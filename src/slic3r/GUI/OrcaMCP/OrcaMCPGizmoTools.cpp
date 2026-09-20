// [ORCAPORT:ME-1] Embedded-MCP tools for mesh hole features and gizmo control.
// find_holes runs the HoleDetector headlessly; the *_gizmo tools open and drive the
// horizontal-hole picker so the feature can be exercised without clicking.
#include "OrcaMCPServer.hpp"
#include "OrcaMCPCommon.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoHorizontalHoles.hpp"
#include "slic3r/GUI/Selection.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/HoleDetector.hpp"
#include "libslic3r/Point.hpp"

#include <wx/utils.h>

#include <algorithm>
#include <cmath>

namespace Slic3r { namespace GUI {

namespace {

using OrcaMCP::run_on_main_thread;

// Bed-up direction expressed in the object's frame (the same frame raw_mesh() uses).
Vec3d object_up(const ModelObject& mo)
{
    if (mo.instances.empty())
        return Vec3d::UnitZ();
    Vec3d up = mo.instances.front()->get_matrix().linear().inverse() * Vec3d::UnitZ();
    if (!up.allFinite() || up.norm() < 1e-9)
        return Vec3d::UnitZ();
    return up.normalized();
}

nlohmann::json vec3_json(const Vec3d& v) { return nlohmann::json::array({v(0), v(1), v(2)}); }

// A hole is horizontal (the top of its wall is an overhang) when its axis is close to the
// build plane. Both the teardrop and the horizontal partial-bridge remedy only make sense there.
constexpr double HORIZONTAL_COS = 0.5;

ModelObject* resolve_object(Plater* plater, int object_id)
{
    Model& model = plater->model();
    int    oid   = object_id;
    if (oid < 0) {
        GLCanvas3D* canvas = plater->get_view3D_canvas3D();
        if (canvas != nullptr)
            oid = canvas->get_selection().get_object_idx();
    }
    if (oid < 0 && model.objects.size() == 1)
        oid = 0;
    if (oid < 0 || oid >= int(model.objects.size()))
        return nullptr;
    return model.objects[oid];
}

int resolved_object_id(Plater* plater, int object_id)
{
    Model& model = plater->model();
    if (object_id >= 0 && object_id < int(model.objects.size()))
        return object_id;
    GLCanvas3D* canvas = plater->get_view3D_canvas3D();
    if (canvas != nullptr) {
        const int sel = canvas->get_selection().get_object_idx();
        if (sel >= 0 && sel < int(model.objects.size()))
            return sel;
    }
    return model.objects.size() == 1 ? 0 : -1;
}

nlohmann::json find_holes_json(int object_id, bool include_vertical)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return {{"status", "error"}, {"error", "No plater"}};

    const int oid = resolved_object_id(plater, object_id);
    ModelObject* mo = resolve_object(plater, object_id);
    if (mo == nullptr)
        return {{"status", "error"}, {"error", "No object (load a model or pass object_id)"}};

    wxBusyCursor    wait;
    const Vec3d     up    = object_up(*mo);
    TriangleMesh    mesh  = mo->raw_mesh();
    std::vector<DetectedHole> holes = detect_holes(mesh.its);

    nlohmann::json arr = nlohmann::json::array();
    int            kept = 0;
    for (size_t i = 0; i < holes.size(); ++i) {
        const DetectedHole& h          = holes[i];
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
            {"eligible_partial_bridge", horizontal},
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

nlohmann::json horizontal_gizmo_state(GLGizmoHorizontalHoles &g)
{
    const int      n   = g.hole_count(); // runs detection on demand
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
            {"has_teardrop", g.hole_has_teardrop(i)},
            {"has_bridge", g.hole_has_bridge(i)},
        });
    }
    return {
        {"hole_count", n},
        {"bridge_mode", g.bridge_mode()},
        {"angle", g.get_angle()},
        {"holes", arr},
    };
}

// Find the horizontal-holes gizmo, activating it if requested. Returns nullptr when it is not
// the active gizmo (or cannot become active, e.g. no single object selected).
GLGizmoHorizontalHoles* active_horizontal_holes_gizmo(bool open_if_needed)
{
    Plater*      plater = wxGetApp().plater();
    GLCanvas3D*  canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
    if (canvas == nullptr)
        return nullptr;
    GLGizmosManager& mgr = canvas->get_gizmos_manager();

    if (open_if_needed && mgr.get_current_type() != GLGizmosManager::EType::HorizontalHoles) {
        // The gizmo is only activable on a single full instance; select one if needed.
        Selection& sel = canvas->get_selection();
        if (!sel.is_single_full_instance() && !plater->model().objects.empty()) {
            int oid = sel.get_object_idx();
            if (oid < 0)
                oid = 0;
            if (oid >= 0 && oid < int(plater->model().objects.size())) {
                sel.clear();
                sel.add_object(unsigned(oid), true);
            }
        }
        mgr.open_gizmo(GLGizmosManager::EType::HorizontalHoles);
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }

    if (mgr.get_current_type() != GLGizmosManager::EType::HorizontalHoles)
        return nullptr;
    return dynamic_cast<GLGizmoHorizontalHoles*>(mgr.get_current());
}

} // namespace

void OrcaMCPServer::register_gizmo_tools()
{
    register_tool({
        "find_holes",
        "Detect cylindrical holes on a model object's mesh (no CAD reconstruction). Returns axis, "
        "center, radius, depth, through/blind and whether each hole is horizontal, i.e. eligible "
        "for a teardrop or a horizontal partial bridge. Omit object_id to use the selection.",
        {
            {"type", "object"},
            {"properties", {
                {"object_id", {{"type", "integer"}, {"description", "Object index (0-based); omit for the selected object."}}},
                {"include_vertical", {{"type", "boolean"}, {"description", "Also return vertical holes (default false)."}}}
            }}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            const int  object_id        = params.contains("object_id") ? params["object_id"].get<int>() : -1;
            const bool include_vertical = params.value("include_vertical", false);
            return run_on_main_thread([object_id, include_vertical]() -> nlohmann::json {
                return find_holes_json(object_id, include_vertical);
            });
        }
    });

    register_tool({
        "horizontal_holes_gizmo",
        "Control the horizontal-hole picker gizmo. Actions: 'open' (activate it; selects the object "
        "if needed), 'status' (detected holes + which are teardropped / partial-bridged), "
        "'set_angle' (apex angle, clamped), 'set_mode' (teardrop|bridge), 'toggle' (one hole by "
        "index), 'apply_all', 'clear_all', 'refresh', 'close'. Use find_holes first for indices.",
        {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"description", "open | status | set_angle | set_mode | toggle | apply_all | clear_all | refresh | close"}}},
                {"object_id", {{"type", "integer"}, {"description", "Object to select when opening (default: current selection or the only object)."}}},
                {"hole_index", {{"type", "integer"}, {"description", "Hole index for action=toggle."}}},
                {"angle", {{"type", "number"}, {"description", "Apex angle in degrees for action=set_angle."}}},
                {"mode", {{"type", "string"}, {"description", "teardrop | bridge, for action=set_mode."}}}
            }},
            {"required", {"action"}}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            const std::string action = params.value("action", std::string("status"));
            return run_on_main_thread([action, params]() -> nlohmann::json {
                Plater*     plater = wxGetApp().plater();
                GLCanvas3D* canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
                if (canvas == nullptr)
                    return {{"status", "error"}, {"error", "No 3D canvas"}};

                if (action == "close") {
                    canvas->reset_all_gizmos();
                    canvas->set_as_dirty();
                    return {{"status", "ok"}, {"open", false}, {"action", action}};
                }

                // Select the requested object before opening, when given.
                if (params.contains("object_id")) {
                    const int oid = params["object_id"].get<int>();
                    Model&    model = plater->model();
                    if (oid < 0 || oid >= int(model.objects.size()))
                        return {{"status", "error"}, {"error", "Invalid object_id"}};
                    Selection& sel = canvas->get_selection();
                    sel.clear();
                    sel.add_object(unsigned(oid), true);
                }

                GLGizmoHorizontalHoles* g = active_horizontal_holes_gizmo(action == "open");
                if (g == nullptr)
                    return {{"status", "error"},
                            {"error", "Horizontal-holes gizmo is not active. Load a model and pass object_id or select a single object."}};

                if (action == "set_angle") {
                    if (!params.contains("angle"))
                        return {{"status", "error"}, {"error", "action=set_angle needs 'angle'"}};
                    g->set_angle(params["angle"].get<float>());
                } else if (action == "set_mode") {
                    g->set_bridge_mode(params.value("mode", std::string("teardrop")) == "bridge");
                } else if (action == "toggle") {
                    if (!params.contains("hole_index"))
                        return {{"status", "error"}, {"error", "action=toggle needs 'hole_index'"}};
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

                nlohmann::json result = horizontal_gizmo_state(*g);
                result["status"] = "ok";
                result["open"]   = true;
                result["action"] = action;
                return result;
            });
        }
    });
}

}} // namespace Slic3r::GUI
