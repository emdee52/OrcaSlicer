// [ORCAPORT:ME-2] Embedded-MCP tools for mesh hole features and gizmo control.
// find_holes runs the HoleDetector headlessly; holes_gizmo opens and drives the Holes tool so the
// feature can be exercised without clicking.
#include "OrcaMCPServer.hpp"
#include "OrcaMCPCommon.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoHoles.hpp"
#include "slic3r/GUI/Selection.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/HoleDetector.hpp"
#include "libslic3r/HoleStandards.hpp"
#include "libslic3r/Point.hpp"

#include <wx/utils.h>

#include <algorithm>
#include <cmath>

namespace Slic3r { namespace GUI {

namespace {

using OrcaMCP::run_on_main_thread;

Vec3d object_up(const ModelObject &mo)
{
    if (mo.instances.empty())
        return Vec3d::UnitZ();
    Vec3d up = mo.instances.front()->get_matrix().linear().inverse() * Vec3d::UnitZ();
    if (!up.allFinite() || up.norm() < 1e-9)
        return Vec3d::UnitZ();
    return up.normalized();
}

nlohmann::json vec3_json(const Vec3d &v) { return nlohmann::json::array({v(0), v(1), v(2)}); }

constexpr double HORIZONTAL_COS = 0.5;

int resolved_object_id(Plater *plater, int object_id)
{
    Model &model = plater->model();
    if (object_id >= 0 && object_id < int(model.objects.size()))
        return object_id;
    GLCanvas3D *canvas = plater->get_view3D_canvas3D();
    if (canvas != nullptr) {
        const int sel = canvas->get_selection().get_object_idx();
        if (sel >= 0 && sel < int(model.objects.size()))
            return sel;
    }
    return model.objects.size() == 1 ? 0 : -1;
}

nlohmann::json find_holes_json(int object_id, bool include_vertical)
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return {{"status", "error"}, {"error", "No plater"}};

    const int oid = resolved_object_id(plater, object_id);
    if (oid < 0)
        return {{"status", "error"}, {"error", "No object (load a model or pass object_id)"}};
    ModelObject *mo = plater->model().objects[oid];

    wxBusyCursor wait;
    const Vec3d up = object_up(*mo);
    TriangleMesh mesh = mo->raw_mesh();
    std::vector<DetectedHole> holes = detect_holes(mesh.its);

    nlohmann::json arr = nlohmann::json::array();
    int            kept = 0;
    for (size_t i = 0; i < holes.size(); ++i) {
        const DetectedHole &h          = holes[i];
        const bool          horizontal = std::abs(h.axis.dot(up)) < HORIZONTAL_COS;
        if (!include_vertical && !horizontal)
            continue;
        arr.push_back({
            {"index", i},
            {"axis", vec3_json(h.axis)},
            {"center", vec3_json(h.center)},
            {"radius", h.radius},
            {"diameter", 2.0 * h.radius},
            {"depth", h.depth},
            {"through", h.through},
            {"horizontal", horizontal},
            {"confidence", h.confidence},
            {"facet_count", int(h.facets.size())},
            {"eligible_teardrop", horizontal},
            {"eligible_bore", true},
        });
        ++kept;
    }

    return {
        {"status", "ok"},
        {"object_id", oid},
        {"object_name", mo->name},
        {"holes_total", int(holes.size())},
        {"holes_returned", kept},
        {"holes", arr},
    };
}

GLGizmoHoles *active_holes_gizmo(bool open_if_needed)
{
    Plater     *plater = wxGetApp().plater();
    GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
    if (canvas == nullptr)
        return nullptr;
    GLGizmosManager &mgr = canvas->get_gizmos_manager();

    if (open_if_needed && mgr.get_current_type() != GLGizmosManager::EType::Holes) {
        Selection &sel = canvas->get_selection();
        if (!sel.is_single_full_instance() && !plater->model().objects.empty()) {
            int oid = sel.get_object_idx();
            if (oid < 0)
                oid = 0;
            if (oid >= 0 && oid < int(plater->model().objects.size())) {
                sel.clear();
                sel.add_object(unsigned(oid), true);
            }
        }
        mgr.open_gizmo(GLGizmosManager::EType::Holes);
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }

    if (mgr.get_current_type() != GLGizmosManager::EType::Holes)
        return nullptr;
    return dynamic_cast<GLGizmoHoles *>(mgr.get_current());
}

nlohmann::json holes_gizmo_state(GLGizmoHoles &g)
{
    const int      n   = g.hole_count();
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < n; ++i) {
        const DetectedHole h = g.hole(i);
        arr.push_back({
            {"index", i},
            {"axis", vec3_json(h.axis)},
            {"center", vec3_json(h.center)},
            {"radius", h.radius},
            {"diameter", 2.0 * h.radius},
            {"depth", h.depth},
            {"through", h.through},
            {"horizontal", g.hole_horizontal(i)},
            {"has_teardrop", g.hole_has_teardrop(i)},
            {"has_bore", g.hole_has_bore(i)},
        });
    }
    const char *op = g.get_operation() == HoleOperation::Teardrop ? "teardrop" : "bore";
    const char *cat = "custom";
    switch (g.get_category()) {
    case HoleCategory::Screw:  cat = "screw"; break;
    case HoleCategory::Nut:    cat = "nut"; break;
    case HoleCategory::Magnet: cat = "magnet"; break;
    case HoleCategory::Insert: cat = "insert"; break;
    case HoleCategory::Custom: cat = "custom"; break;
    }

    // Volume bookkeeping helps diagnose whether features are actually being added.
    int         volume_count = -1, pocket_volumes = 0;
    Plater     *plater = wxGetApp().plater();
    GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
    if (plater != nullptr && canvas != nullptr) {
        const int oi = canvas->get_selection().get_object_idx();
        if (oi >= 0 && oi < int(plater->model().objects.size())) {
            const ModelObject *mo = plater->model().objects[oi];
            volume_count          = int(mo->volumes.size());
            for (const ModelVolume *v : mo->volumes)
                if (v->name.rfind("HolePocket", 0) == 0)
                    ++pocket_volumes;
        }
    }

    nlohmann::json heights = nlohmann::json::array();
    if (g.get_standard() >= 0 && g.get_standard() < int(hole_standards().size()))
        for (double hv : hole_standards()[g.get_standard()].insert_heights)
            heights.push_back(hv);

    return {
        {"hole_count", n},
        {"operation", op},
        {"category", cat},
        {"angle", g.get_angle()},
        {"standard_index", g.get_standard() + 1},
        {"standard_name", g.standard_name(g.get_standard() + 1)},
        {"diameter", g.get_diameter()},
        {"tolerance", g.get_tolerance()},
        {"depth", g.get_depth()},
        {"head_fit", g.get_head_fit()},
        {"true_diameter", g.true_diameter()},
        {"screw_fit", g.get_screw_fit() == ScrewFit::Tap ? "tap" : "free"},
        {"volume_count", volume_count},
        {"pocket_volumes", pocket_volumes},
        {"heights", heights},
        {"holes", arr},
    };
}

} // namespace

void OrcaMCPServer::register_gizmo_tools()
{
    register_tool({
        "find_holes",
        "Detect cylindrical holes on a model object's mesh (no CAD reconstruction). Returns axis, "
        "center, radius, depth, through/blind and whether each hole is horizontal (eligible for a "
        "teardrop; every hole is eligible for a bore/pocket). Omit object_id for the selection.",
        {
            {"type", "object"},
            {"properties", {
                {"object_id", {{"type", "integer"}, {"description", "Object index (0-based); omit for the selected object."}}},
                {"include_vertical", {{"type", "boolean"}, {"description", "Also return vertical holes (default false)."}}}
            }}
        },
        [](const nlohmann::json &params) -> nlohmann::json {
            const int  object_id        = params.contains("object_id") ? params["object_id"].get<int>() : -1;
            const bool include_vertical = params.value("include_vertical", false);
            return run_on_main_thread([object_id, include_vertical]() -> nlohmann::json {
                return find_holes_json(object_id, include_vertical);
            });
        }
    });

    register_tool({
        "holes_gizmo",
        "Control the Holes tool. Actions: 'open' (activate it; selects the object if needed), "
        "'status' (detected holes + which are teardropped / bored), 'set_operation' "
        "(teardrop|bore), 'set_angle', 'set_standard' (0=Custom, else 1-based index into the "
        "standards list), 'set_head' (none|counterbore|countersink), 'set_fit' "
        "(tight|slip), 'set_diameter', 'set_through', 'set_flip', 'toggle' (one hole by "
        "index), 'apply_all', 'clear_all', 'refresh', 'close'. Use find_holes first for indices.",
        {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"description", "open | status | set_operation | set_category | set_angle | set_standard | set_head | set_screw_fit | set_fit | set_diameter | set_tolerance | set_head_fit | set_through | set_depth | set_flip | toggle | apply_all | clear_all | refresh | close"}}},
                {"object_id", {{"type", "integer"}, {"description", "Object to select when opening."}}},
                {"hole_index", {{"type", "integer"}, {"description", "Hole index for action=toggle."}}},
                {"operation", {{"type", "string"}, {"description", "teardrop | bore, for action=set_operation."}}},
                {"category", {{"type", "string"}, {"description", "screw | nut | magnet | insert | custom, for action=set_category."}}},
                {"angle", {{"type", "number"}, {"description", "Apex angle (45-60) for action=set_angle."}}},
                {"standard_index", {{"type", "integer"}, {"description", "0=Custom, else 1-based standard index, for action=set_standard."}}},
                {"head", {{"type", "string"}, {"description", "none | socket | button | countersink, for action=set_head."}}},
                {"screw_fit", {{"type", "string"}, {"description", "free | tap, for action=set_screw_fit (screws only)."}}},
                {"fit", {{"type", "string"}, {"description", "tight | slip, for action=set_fit."}}},
                {"diameter", {{"type", "number"}, {"description", "Nominal diameter mm, for action=set_diameter. Matching a standard's size selects it."}}},
                {"tolerance", {{"type", "number"}, {"description", "Extra diameter mm, for action=set_tolerance (ignored for insert/magnet, which derive it from the fit)."}}},
                {"depth", {{"type", "number"}, {"description", "Pocket / blind depth mm, for action=set_depth."}}},
                {"head_fit", {{"type", "number"}, {"description", "0, -0.08 or -0.16 mm, for action=set_head_fit (screws/nuts/magnets)."}}},
                {"through", {{"type", "boolean"}, {"description", "For action=set_through."}}},
                {"flip", {{"type", "boolean"}, {"description", "For action=set_flip."}}}
            }},
            {"required", {"action"}}
        },
        [](const nlohmann::json &params) -> nlohmann::json {
            const std::string action = params.value("action", std::string("status"));
            return run_on_main_thread([action, params]() -> nlohmann::json {
                Plater     *plater = wxGetApp().plater();
                GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
                if (canvas == nullptr)
                    return {{"status", "error"}, {"error", "No 3D canvas"}};

                if (action == "close") {
                    canvas->reset_all_gizmos();
                    canvas->set_as_dirty();
                    return {{"status", "ok"}, {"open", false}, {"action", action}};
                }

                if (params.contains("object_id")) {
                    const int oid = params["object_id"].get<int>();
                    if (oid < 0 || oid >= int(plater->model().objects.size()))
                        return {{"status", "error"}, {"error", "Invalid object_id"}};
                    Selection &sel = canvas->get_selection();
                    sel.clear();
                    sel.add_object(unsigned(oid), true);
                }

                GLGizmoHoles *g = active_holes_gizmo(action == "open");
                if (g == nullptr)
                    return {{"status", "error"},
                            {"error", "Holes gizmo is not active. Load a model and pass object_id or select a single object."}};

                auto need = [&](const char *key) { return params.contains(key); };

                if (action == "set_operation") {
                    g->set_operation(params.value("operation", std::string("teardrop")) == "bore" ? HoleOperation::Bore
                                                                                                 : HoleOperation::Teardrop);
                } else if (action == "set_category") {
                    const std::string c = params.value("category", std::string("custom"));
                    g->set_category(c == "screw" ? HoleCategory::Screw
                                   : c == "nut" ? HoleCategory::Nut
                                   : c == "magnet" ? HoleCategory::Magnet
                                   : c == "insert" ? HoleCategory::Insert
                                                   : HoleCategory::Custom);
                } else if (action == "set_angle") {
                    if (!need("angle")) return {{"status", "error"}, {"error", "needs 'angle'"}};
                    g->set_angle(params["angle"].get<float>());
                } else if (action == "set_standard") {
                    if (!need("standard_index")) return {{"status", "error"}, {"error", "needs 'standard_index'"}};
                    g->set_standard(params["standard_index"].get<int>());
                } else if (action == "set_head") {
                    const std::string h = params.value("head", std::string("none"));
                    g->set_head(h == "socket" ? BoreHead::SocketHead
                                       : h == "button" ? BoreHead::ButtonHead
                                       : h == "countersink" ? BoreHead::Countersink
                                                            : BoreHead::None);
                } else if (action == "set_screw_fit") {
                    g->set_screw_fit(params.value("screw_fit", std::string("free")) == "tap" ? ScrewFit::Tap : ScrewFit::Free);
                } else if (action == "set_fit") {
                    const std::string f = params.value("fit", std::string("slip"));
                    g->set_fit(f == "tight" ? HoleFit::Tight : HoleFit::Slip);
                } else if (action == "set_diameter") {
                    if (!need("diameter")) return {{"status", "error"}, {"error", "needs 'diameter'"}};
                    g->set_diameter(params["diameter"].get<double>());
                } else if (action == "set_tolerance") {
                    if (!need("tolerance")) return {{"status", "error"}, {"error", "needs 'tolerance'"}};
                    g->set_tolerance(params["tolerance"].get<double>());
                } else if (action == "set_head_fit") {
                    if (!need("head_fit")) return {{"status", "error"}, {"error", "needs 'head_fit'"}};
                    g->set_head_fit(params["head_fit"].get<double>());
                } else if (action == "set_through") {
                    g->set_through(params.value("through", true));
                } else if (action == "set_depth") {
                    if (!need("depth")) return {{"status", "error"}, {"error", "needs 'depth'"}};
                    g->set_depth(params["depth"].get<double>());
                } else if (action == "set_flip") {
                    g->set_flip(params.value("flip", false));
                } else if (action == "toggle") {
                    if (!need("hole_index")) return {{"status", "error"}, {"error", "needs 'hole_index'"}};
                    g->gizmo_toggle_hole(params["hole_index"].get<int>());
                } else if (action == "apply_all") {
                    g->gizmo_apply_all();
                } else if (action == "clear_all") {
                    g->gizmo_clear_all();
                } else if (action == "refresh") {
                    g->gizmo_refresh();
                } else if (action != "status" && action != "open") {
                    return {{"status", "error"}, {"error", "Unknown action: " + action}};
                }

                nlohmann::json result = holes_gizmo_state(*g);
                result["status"] = "ok";
                result["open"]   = true;
                result["action"] = action;
                return result;
            });
        }
    });
}

}} // namespace Slic3r::GUI
