// [ORCAPORT:ME-2] Embedded-MCP tools for mesh hole features and gizmo control.
// find_holes runs the HoleDetector headlessly; holes_gizmo opens and drives the Holes tool so the
// feature can be exercised without clicking.
// [ORCAPORT:CUT-1] cut_gizmo opens and drives the Cut tool, including aligning the cut plane to a
// face by its normal (the interactive "Pick flat face" mode uses the same code path).
#include "OrcaMCPServer.hpp"
#include "OrcaMCPCommon.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoHoles.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoCut.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoEdgeDress.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoExtrude.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPerforation.hpp" // [ORCAPORT:PP-1]
#include "slic3r/GUI/Selection.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/HoleDetector.hpp"
#include "libslic3r/HoleStandards.hpp"
#include "libslic3r/Point.hpp"

#include <imgui/imgui.h>

#include <wx/utils.h>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <wx/glcanvas.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/wfstream.h>
#include <wx/base64.h>
#include <wx/datetime.h>
#include <wx/stdpaths.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

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
{    const int      n   = g.hole_count();
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
    const char *op = g.get_operation() == HoleOperation::Teardrop ? "teardrop"
                   : g.get_operation() == HoleOperation::Bore ? "bore"
                                                             : "chamfer_fillet";
    const char *cat = "screw";
    switch (g.get_category()) {
    case HoleCategory::Screw:  cat = "screw"; break;
    case HoleCategory::Nut:    cat = "nut"; break;
    case HoleCategory::Magnet: cat = "magnet"; break;
    case HoleCategory::Insert: cat = "insert"; break;
    }

    // Volume bookkeeping helps diagnose whether features are actually being added.
    int         volume_count = -1, pocket_volumes = 0, face_pocket_volumes = 0;
    Plater     *plater = wxGetApp().plater();
    GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
    if (plater != nullptr && canvas != nullptr) {
        const int oi = canvas->get_selection().get_object_idx();
        if (oi >= 0 && oi < int(plater->model().objects.size())) {
            const ModelObject *mo = plater->model().objects[oi];
            volume_count          = int(mo->volumes.size());
            for (const ModelVolume *v : mo->volumes) {
                if (v->name.rfind("HolePocket", 0) == 0)
                    ++pocket_volumes;
                if (v->name.rfind("FacePocket", 0) == 0)
                    ++face_pocket_volumes;
            }
        }
    }

    nlohmann::json placed = nlohmann::json::array();
    for (int i = 0; i < g.placed_face_count(); ++i) {
        const DetectedHole h = g.placed_face(i);
        placed.push_back({{"id", g.placed_face_id(i)},
                          {"axis", vec3_json(h.axis)},
                          {"center", vec3_json(h.center)},
                          {"diameter", 2.0 * h.radius},
                          {"diameter_world", 2.0 * h.radius * g.object_scale()},
                          {"depth", h.depth},
                          {"depth_world", h.depth * g.object_scale()},
                          {"through", h.through}});
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
        {"screw_teardrop", g.get_screw_teardrop()},
        {"screw_teardrop_head", g.get_screw_teardrop_head()},
        {"rim_type", g.get_rim_fillet() ? "fillet" : "chamfer"},
        {"rim_both", g.get_rim_both()},
        {"volume_count", volume_count},
        {"pocket_volumes", pocket_volumes},
        {"face_pocket_volumes", face_pocket_volumes},
        {"object_scale", g.object_scale()},
        {"place_face_mode", g.place_face_mode()},
        {"placed_faces", placed},
        {"heights", heights},
        {"holes", arr},
    };
}

bool json_vec3(const nlohmann::json &j, Vec3d &out)
{
    if (!j.is_array() || j.size() != 3)
        return false;
    out = Vec3d(j[0].get<double>(), j[1].get<double>(), j[2].get<double>());
    return true;
}

GLGizmoCut3D *active_cut_gizmo(bool open_if_needed)
{
    Plater     *plater = wxGetApp().plater();
    GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
    if (canvas == nullptr)
        return nullptr;
    GLGizmosManager &mgr = canvas->get_gizmos_manager();

    if (open_if_needed && mgr.get_current_type() != GLGizmosManager::EType::Cut) {
        Selection &sel = canvas->get_selection();
        if (!sel.is_single_full_instance() && !sel.is_single_volume() && !plater->model().objects.empty()) {
            int oid = sel.get_object_idx();
            if (oid < 0)
                oid = 0;
            if (oid >= 0 && oid < int(plater->model().objects.size())) {
                sel.clear();
                sel.add_object(unsigned(oid), true);
            }
        }
        mgr.open_gizmo(GLGizmosManager::EType::Cut);
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }

    if (mgr.get_current_type() != GLGizmosManager::EType::Cut)
        return nullptr;
    return dynamic_cast<GLGizmoCut3D *>(mgr.get_current());
}

// [ORCAPORT:EF-1] Open and drive the Edge chamfer / fillet tool.
GLGizmoEdgeDress *active_edge_dress_gizmo(bool open_if_needed)
{
    Plater     *plater = wxGetApp().plater();
    GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
    if (canvas == nullptr)
        return nullptr;
    GLGizmosManager &mgr = canvas->get_gizmos_manager();

    if (open_if_needed && mgr.get_current_type() != GLGizmosManager::EType::EdgeDress) {
        Selection &sel = canvas->get_selection();
        if (!sel.is_single_full_instance() && !sel.is_single_volume() && !plater->model().objects.empty()) {
            int oid = sel.get_object_idx();
            if (oid < 0)
                oid = 0;
            if (oid >= 0 && oid < int(plater->model().objects.size())) {
                sel.clear();
                sel.add_object(unsigned(oid), true);
            }
        }
        mgr.open_gizmo(GLGizmosManager::EType::EdgeDress);
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }

    if (mgr.get_current_type() != GLGizmosManager::EType::EdgeDress)
        return nullptr;
    return dynamic_cast<GLGizmoEdgeDress *>(mgr.get_current());
}

// [ORCAPORT:HF-1] Open and drive the Cavity fill tool.
GLGizmoExtrude *active_extrude_gizmo(bool open_if_needed)
{
    Plater     *plater = wxGetApp().plater();
    GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
    if (canvas == nullptr)
        return nullptr;
    GLGizmosManager &mgr = canvas->get_gizmos_manager();

    if (open_if_needed && mgr.get_current_type() != GLGizmosManager::EType::Extrude) {
        Selection &sel = canvas->get_selection();
        if (!sel.is_single_full_instance() && !sel.is_single_volume() && !plater->model().objects.empty()) {
            int oid = sel.get_object_idx();
            if (oid < 0)
                oid = 0;
            if (oid >= 0 && oid < int(plater->model().objects.size())) {
                sel.clear();
                sel.add_object(unsigned(oid), true);
            }
        }
        mgr.open_gizmo(GLGizmosManager::EType::Extrude);
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }

    if (mgr.get_current_type() != GLGizmosManager::EType::Extrude)
        return nullptr;
    return dynamic_cast<GLGizmoExtrude *>(mgr.get_current());
}

nlohmann::json edge_dress_gizmo_state(GLGizmoEdgeDress &g)
{
    return nlohmann::json{
        {"mode", g.get_mode() == EdgeDressMode::Chamfer ? "chamfer" : "fillet"},
        {"size", g.get_size()},
        {"hover_edge_valid", g.hover_edge_valid()},
        {"hover_applied_volume", g.hover_applied_volume()},
        {"loop_mode", g.loop_mode()},
        {"loop_facet", g.loop_facet()},
        {"applied_count", g.applied_count()},
        {"object_scale", g.object_scale()}};
}

nlohmann::json extrude_gizmo_state(GLGizmoExtrude &g)
{
    return nlohmann::json{
        {"mode", g.get_mode() == GLGizmoExtrude::Mode::ExtrudeFace ? "extrude_face" : "cavity_fill"},
        {"radius", g.get_radius()},
        {"hover_valid", g.hover_valid()},
        {"pick_valid", g.pick_valid()},
        {"height", g.get_extrude_height()},
        {"offset", g.get_extrude_offset()},
        {"holes", g.get_extrude_holes()},
        {"both", g.get_extrude_both()},
        {"applied_count", g.applied_count()},
        {"object_scale", g.object_scale()}};
}

nlohmann::json cut_gizmo_state(GLGizmoCut3D &g, GLCanvas3D *canvas)
{
    // CUT-2: list the model parts that can be cut individually.
    nlohmann::json parts = nlohmann::json::array();
    int            object_id = canvas != nullptr ? canvas->get_selection().get_object_idx() : -1;
    if (canvas != nullptr && object_id >= 0 && object_id < int(canvas->get_selection().get_model()->objects.size())) {
        const ModelObject *mo = canvas->get_selection().get_model()->objects[object_id];
        for (size_t i = 0; i < mo->volumes.size(); ++i)
            if (mo->volumes[i]->is_model_part() && !mo->volumes[i]->is_cut_connector())
                parts.push_back({{"index", int(i)}, {"name", mo->volumes[i]->name}});
    }

    return {
        {"mode", g.gizmo_get_mode()},
        {"keep_upper", g.gizmo_keep_upper()},
        {"keep_lower", g.gizmo_keep_lower()},
        {"keep_as_parts", g.gizmo_keep_as_parts()},
        {"plane_normal", vec3_json(g.gizmo_plane_normal())},
        {"plane_center", vec3_json(g.gizmo_plane_center())},
        {"plane_offset", g.gizmo_plane_offset()},
        {"pick_face_mode", g.gizmo_pick_face_mode()},
        {"single_part", g.gizmo_single_part()},
        {"part", g.gizmo_cut_volume()},
        {"parts", parts},
        {"shape_kind", g.gizmo_shape_kind()},
        {"shape_size", g.gizmo_shape_size()},
        {"shape_through", g.gizmo_shape_through()},
        {"shape_depth", g.gizmo_shape_depth()},
        {"fill_from_above", g.gizmo_fill_from_above_enabled()},
        {"fill_offset", g.gizmo_fill_offset()},
        {"object_id", object_id},
    };
}

// [ORCAPORT:UI-2] The gizmos a name can refer to, for ui_screenshot. The keys follow the EType names
// lowercased, with this fork's own tools also accepted under the name their panel shows.
GLGizmosManager::EType gizmo_type_from_name(const std::string &name)
{
    using E = GLGizmosManager::EType;
    static const std::map<std::string, E> kNames = {
        {"move", E::Move}, {"rotate", E::Rotate}, {"scale", E::Scale}, {"flatten", E::Flatten},
        {"cut", E::Cut}, {"mesh_boolean", E::MeshBoolean}, {"fdm_supports", E::FdmSupports},
        {"seam", E::Seam}, {"fuzzy_skin", E::FuzzySkin}, {"mm_segmentation", E::MmSegmentation},
        {"texture_displacement", E::TextureDisplacement}, {"emboss", E::Emboss}, {"svg", E::Svg},
        {"measure", E::Measure}, {"assembly", E::Assembly}, {"simplify", E::Simplify},
        {"brim_ears", E::BrimEars},
        // this fork's own
        {"align_stack", E::AlignStack}, {"support_zones", E::SupportZones},
        {"counterbore_bridge", E::CounterboreBridge}, {"hole_walls", E::CounterboreBridge},
        {"holes", E::Holes}, {"edge_dress", E::EdgeDress}, {"edge_chamfer", E::EdgeDress},
        {"extrude", E::Extrude}, {"cavity_fill", E::Extrude}, {"perforation", E::Perforation},
    };
    const auto it = kNames.find(name);
    return it == kNames.end() ? E::Undefined : it->second;
}

// Selects the object a gizmo needs before it can be opened, the same way the driving tools do: the
// fork's tools are only activatable with a single full instance selected.
void select_object_for_gizmo(int object_id)
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    GLCanvas3D *canvas = plater->get_view3D_canvas3D();
    if (canvas == nullptr)
        return;
    Selection &sel = canvas->get_selection();
    int        oid = object_id;
    if (oid < 0)
        oid = sel.get_object_idx();
    if (oid < 0)
        oid = 0;
    if (oid >= 0 && oid < int(plater->model().objects.size())) {
        sel.clear();
        sel.add_object(unsigned(oid), true);
    }
}

// The window screenshot, as a PNG in memory. A wxScreenDC blit of the window's screen rect is the
// only way to get BOTH halves of the UI in one image: the wxWidgets chrome and the gizmo panels,
// which are ImGui drawn into the GL canvas and so are invisible to any 3D-view or framebuffer read.
//
// 🚨 Requires the window to be on screen and unoccluded - a blit copies whatever is on the desktop
// there. Pass raise=true (the default) to bring the app forward first.
bool capture_top_window_png(std::vector<unsigned char> &out, int &w, int &h, std::string &err)
{
    wxWindow *win = wxGetApp().GetTopWindow();
    if (win == nullptr) {
        err = "No top-level window";
        return false;
    }
    const wxRect rect = win->GetScreenRect();
    if (rect.width <= 0 || rect.height <= 0) {
        err = "The window has no size (minimised?)";
        return false;
    }

    wxScreenDC screen;
    wxBitmap   bmp(rect.width, rect.height, 24);
    {
        wxMemoryDC mem(bmp);
        if (! mem.Blit(0, 0, rect.width, rect.height, &screen, rect.x, rect.y)) {
            err = "Screen blit failed";
            return false;
        }
    }
    const wxImage img = bmp.ConvertToImage();
    w = img.GetWidth();
    h = img.GetHeight();

    wxMemoryOutputStream stream;
    if (! img.SaveFile(stream, wxBITMAP_TYPE_PNG)) {
        err = "PNG encode failed";
        return false;
    }
    wxStreamBuffer *buf = stream.GetOutputStreamBuffer();
    out.assign(static_cast<unsigned char *>(buf->GetBufferStart()),
               static_cast<unsigned char *>(buf->GetBufferStart()) + buf->GetBufferSize());
    return true;
}

// [ORCAPORT:UI-2] The gizmo panel's rect in *screen* pixels. The panel's own rect is in ImGui pixels,
// which Orca sets to the canvas's physical size (set_display_size(canvas_size)), so the panel's
// offset within the canvas IS a physical pixel offset and only the canvas's screen origin has to be
// added. Anything that is not the same on both sides is reported, so a wrong crop can be diagnosed
// from the numbers instead of guessed at.
bool gizmo_panel_screen_rect(GLCanvas3D *canvas, wxRect &out, std::string &err)
{
    GLGizmosManager &mgr = canvas->get_gizmos_manager();
    GLGizmoBase     *g   = mgr.get_current();
    if (g == nullptr) {
        err = "No gizmo is open";
        return false;
    }
    const GLGizmoBase::PanelRect p = g->last_input_window_rect();
    if (p.w <= 1.f || p.h <= 1.f) {
        err = "The panel has not been laid out yet";
        return false;
    }
    wxGLCanvas *gl = canvas->get_wxglcanvas();
    if (gl == nullptr) {
        err = "No GL canvas widget";
        return false;
    }
    const wxPoint origin = gl->GetScreenPosition();
    out = wxRect(int(std::lround(origin.x + p.x)), int(std::lround(origin.y + p.y)),
                 int(std::lround(p.w)), int(std::lround(p.h)));

    // The canvas must be the size ImGui was told about, or the offset above is in the wrong space.
    const Size cnv = canvas->get_canvas_size();
    const float disp_w = ImGui::GetIO().DisplaySize.x;
    if (disp_w > 1.f && std::abs(disp_w - float(cnv.get_width())) > 1.5f) {
        err = "ImGui display size does not match the canvas (" + std::to_string(int(disp_w)) + " vs " +
              std::to_string(cnv.get_width()) + ")";
        return false;
    }
    return true;
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
        "(teardrop|bore|chamfer_fillet), 'set_angle', 'set_rim_size', 'set_rim_type' "
        "(chamfer|fillet), 'set_rim_both' (both mouths), "
        "'set_standard' (0=Custom, else 1-based index into the "
        "standards list), 'set_head' (none|counterbore|countersink), 'set_fit' "
        "(tight|slip), 'set_diameter', 'set_screw_teardrop' / 'set_screw_teardrop_head' (teardrop a "
        "screw's horizontal bore and optionally its counterbore head), 'set_through', 'set_flip', "
        "'toggle' (one hole by index), 'apply_all', 'clear_all', 'refresh', 'close'. "
        "Use find_holes first for indices.",
        {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"description", "open | status | set_operation | set_category | set_angle | set_rim_size | set_rim_type | set_rim_both | set_standard | set_head | set_screw_fit | set_screw_teardrop | set_screw_teardrop_head | set_fit | set_diameter | set_tolerance | set_head_fit | set_through | set_depth | set_flip | toggle | apply_all | clear_all | refresh | set_place_face | place_face | hover_face | close"}}},
                {"object_id", {{"type", "integer"}, {"description", "Object to select when opening."}}},
                {"hole_index", {{"type", "integer"}, {"description", "Hole index for action=toggle."}}},
                {"screen_x", {{"type", "number"}, {"description", "Canvas X for place_face / hover_face. Defaults to the viewport centre."}}},
                {"screen_y", {{"type", "number"}, {"description", "Canvas Y for place_face / hover_face. Defaults to the viewport centre."}}},
                {"on", {{"type", "boolean"}, {"description", "For action=set_place_face / set_screw_teardrop / set_screw_teardrop_head / set_rim_both: enable or disable."}}},
                {"operation", {{"type", "string"}, {"description", "teardrop | bore | chamfer_fillet, for action=set_operation."}}},
                {"size", {{"type", "number"}, {"description", "Chamfer leg / fillet radius mm, for action=set_rim_size."}}},
                {"rim_type", {{"type", "string"}, {"description", "chamfer | fillet, for action=set_rim_type."}}},
                {"category", {{"type", "string"}, {"description", "screw | nut | magnet | insert, for action=set_category."}}},
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
                    const std::string o = params.value("operation", std::string("teardrop"));
                    g->set_operation(o == "chamfer_fillet" ? HoleOperation::ChamferFillet
                                    : o == "bore"          ? HoleOperation::Bore
                                                           : HoleOperation::Teardrop);
                } else if (action == "set_rim_size") {
                    if (!need("size"))
                        return {{"status", "error"}, {"error", "needs 'size'"}};
                    g->set_rim_size(params["size"].get<double>());
                } else if (action == "set_rim_type") {
                    const std::string t = params.value("rim_type", std::string("chamfer"));
                    g->set_rim_fillet(t == "fillet");
                } else if (action == "set_rim_both") {
                    g->set_rim_both(params.value("on", true));
                } else if (action == "set_category") {
                    const std::string c = params.value("category", std::string("screw"));
                    g->set_category(c == "nut" ? HoleCategory::Nut
                                   : c == "magnet" ? HoleCategory::Magnet
                                   : c == "insert" ? HoleCategory::Insert
                                                   : HoleCategory::Screw);
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
                } else if (action == "set_screw_teardrop") {
                    g->set_screw_teardrop(params.value("on", true));
                } else if (action == "set_screw_teardrop_head") {
                    g->set_screw_teardrop_head(params.value("on", true));
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
                } else if (action == "set_place_face") {
                    g->set_place_face_mode(params.value("on", true));
                } else if (action == "place_face" || action == "hover_face") {
                    Vec2d screen;
                    if (need("screen_x") && need("screen_y"))
                        screen = Vec2d(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    else {
                        const std::array<int, 4> vp = wxGetApp().plater()->get_camera().get_viewport();
                        screen = Vec2d(vp[0] + 0.5 * vp[2], vp[1] + 0.5 * vp[3]);
                    }
                    if (action == "hover_face") {
                        int   facet = -1, region = 0;
                        Vec3d normal = Vec3d::Zero();
                        if (!g->gizmo_face_info_at(screen, facet, region, normal))
                            return {{"status", "error"}, {"error", "No face under the screen point"}};
                        return {{"status", "ok"}, {"open", true}, {"action", action}, {"facet", facet},
                                {"region_facets", region}, {"normal", vec3_json(normal)}};
                    }
                    if (!g->place_face_mode())
                        return {{"status", "error"}, {"error", "Face mode is off; call set_place_face first"}};
                    if (!g->gizmo_place_face_at(screen))
                        return {{"status", "error"}, {"error", "No face under the screen point"}};
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

    // [ORCAPORT:PP-1]
    register_tool({
        "perforation_gizmo",
        "Control the Perforations tool: stamp a 2D pattern (lines/grid/honeycomb/circles) "
        "into a picked planar face and add it as one negative volume, cut through the local wall. "
        "Actions: 'open' (activate; selects the object if needed), 'status' (current parameters), "
        "'set_params' (any of pattern, angle, spacing, width, boundary, invert), "
        "'hover_face' (report the facet and coplanar-region size under screen_x/screen_y), "
        "'apply' (perforate the face under screen_x/screen_y, or the viewport centre), 'close'.",
        {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"description", "open | status | set_params | hover_face | apply | close"}}},
                {"object_id", {{"type", "integer"}, {"description", "Object to select when opening."}}},
                {"pattern", {{"type", "string"}, {"description", "lines | grid | honeycomb | circles"}}},
                {"angle", {{"type", "number"}, {"description", "In-plane pattern angle, degrees."}}},
                {"spacing", {{"type", "number"}, {"description", "Hole / slot centre-to-centre spacing, mm."}}},
                {"width", {{"type", "number"}, {"description", "Hole / slot width, mm."}}},
                {"boundary", {{"type", "number"}, {"description", "Boundary inset from the face outline, mm."}}},
                {"invert", {{"type", "boolean"}, {"description", "Subtract the material between holes (grooves)."}}},
                {"screen_x", {{"type", "number"}, {"description", "Canvas X for hover_face / apply. Defaults to the viewport centre."}}},
                {"screen_y", {{"type", "number"}, {"description", "Canvas Y for hover_face / apply."}}}
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

                GLGizmosManager &mgr = canvas->get_gizmos_manager();
                if (action == "open" && mgr.get_current_type() != GLGizmosManager::EType::Perforation) {
                    Selection &sel = canvas->get_selection();
                    if (!sel.is_single_full_instance() && !plater->model().objects.empty()) {
                        int oid = sel.get_object_idx();
                        if (oid < 0) oid = 0;
                        sel.clear();
                        sel.add_object(unsigned(oid), true);
                    }
                    mgr.open_gizmo(GLGizmosManager::EType::Perforation);
                    canvas->set_as_dirty();
                    canvas->request_extra_frame();
                }
                GLGizmoPerforation *g = mgr.get_current_type() == GLGizmosManager::EType::Perforation
                                            ? dynamic_cast<GLGizmoPerforation *>(mgr.get_current())
                                            : nullptr;
                if (g == nullptr)
                    return {{"status", "error"}, {"error", "Perforations gizmo is not active. Load a model and pass object_id."}};

                PerforationParams p = g->params();
                bool params_changed = false;
                if (params.contains("pattern")) {
                    const std::string s = params["pattern"].get<std::string>();
                    p.kind = s == "lines" ? PerforationKind::Lines
                           : s == "grid" ? PerforationKind::Grid
                           : s == "circles" ? PerforationKind::Circles
                                            : PerforationKind::Honeycomb;
                    params_changed = true;
                }
                if (params.contains("angle"))    { p.angle_deg = params["angle"].get<double>();    params_changed = true; }
                if (params.contains("spacing"))  { p.spacing   = params["spacing"].get<double>();  params_changed = true; }
                if (params.contains("width"))    { p.width     = params["width"].get<double>();    params_changed = true; }
                if (params.contains("boundary")) { p.margin    = params["boundary"].get<double>(); params_changed = true; }
                if (params.contains("invert"))   { p.invert    = params["invert"].get<bool>();     params_changed = true; }
                if (params_changed)
                    g->set_params(p);

                auto report = [&](const std::string &act) {
                    const PerforationParams q = g->params();
                    const char *k = q.kind == PerforationKind::Lines ? "lines"
                                  : q.kind == PerforationKind::Grid ? "grid"
                                  : q.kind == PerforationKind::Circles ? "circles" : "honeycomb";
                    return nlohmann::json{
                        {"status", "ok"}, {"open", true}, {"action", act},
                        {"pattern", k}, {"angle", q.angle_deg}, {"spacing", q.spacing},
                        {"width", q.width}, {"boundary", q.margin}, {"invert", q.invert}};
                };

                if (action == "set_params" || action == "status" || action == "open")
                    return report(action);

                Vec2d screen;
                if (params.contains("screen_x") && params.contains("screen_y"))
                    screen = Vec2d(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                else {
                    const std::array<int, 4> vp = plater->get_camera().get_viewport();
                    screen = Vec2d(vp[0] + 0.5 * vp[2], vp[1] + 0.5 * vp[3]);
                }

                if (action == "hover_face") {
                    int facet = -1, region = 0;
                    Vec3d normal = Vec3d::Zero();
                    if (!g->gizmo_face_info_at(screen, facet, region, normal))
                        return {{"status", "error"}, {"error", "No face under the screen point"}};
                    return {{"status", "ok"}, {"open", true}, {"action", action}, {"facet", facet},
                            {"region_facets", region}, {"normal", vec3_json(normal)}};
                }
                if (action == "apply") {
                    if (!g->gizmo_apply_at(screen))
                        return {{"status", "error"}, {"error", "No perfomable face under the screen point"}};
                    return report(action);
                }
                return {{"status", "error"}, {"error", "Unknown action: " + action}};
            });
        }
    });

    register_tool({
        "cut_gizmo",
        "Control the Cut tool. Actions: 'open' (activate it; selects the object if needed; optional "
        "'part' selects a single part), "
        "'status' (plane pose, keep flags, mode, cut part), 'set_part' (cut only one model part of "
        "a multi-part object; -1 = whole object), 'set_plane_normal' (align the cut plane to a "
        "normal, e.g. a face normal - the same path as the interactive 'Pick flat face' mode), "
        "'set_plane_center' (plane point), 'shift_cut' (move the plane along its normal by delta mm), "
        "'set_mode' (0=Planar, 1=Dovetail, 2=Shape), 'set_keep' (keep_upper/keep_lower/keep_as_parts), "
        "'set_shape' (profile kind 0=circle, 1=square, 2=hexagon, size in mm, optional through and "
        "depth in mm; switches to Shape mode), "
        "'flip' (swap upper/lower), 'reset' (reset plane and connectors), 'apply' (perform the cut), "
        "'fill_from_above' (add the slab of the object above the cut plane, lowered by 'offset' mm, as "
        "a positive part - reproduces the manual cut/duplicate/lower workflow without cutting), "
        "'pick_face' (raycast the object and align the plane to the face under screen_x/screen_y, or "
        "the viewport centre if omitted - the same path as the interactive 'Pick flat face' mode), "
        "'hover_face' (report the facet under screen_x/screen_y and how many coplanar facets its "
        "hover highlight covers), "
        "'close'. To cut parallel to a face: set_plane_normal to the face normal, then "
        "set_plane_center to a point on the face, then shift_cut to move it into the part, then apply.",
        {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"description", "open | status | set_part | set_plane_normal | set_plane_center | shift_cut | set_mode | set_keep | set_shape | flip | reset | apply | fill_from_above | pick_face | hover_face | close"}}},
                {"object_id", {{"type", "integer"}, {"description", "Object to select when opening."}}},
                {"part", {{"type", "integer"}, {"description", "Model-volume index (see status.parts) to cut alone, for action=set_part or open. -1 = whole object."}}},
                {"normal", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "[x,y,z] world-space cut-plane normal, for action=set_plane_normal."}}},
                {"center", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "[x,y,z] world-space plane point, for action=set_plane_center."}}},
                {"delta", {{"type", "number"}, {"description", "Signed distance mm to move the plane along its normal, for action=shift_cut."}}},
                {"mode", {{"type", "integer"}, {"description", "0=Planar, 1=Dovetail, 2=Shape, for action=set_mode."}}},
                {"keep_upper", {{"type", "boolean"}, {"description", "Keep the part above the plane, for action=set_keep."}}},
                {"keep_lower", {{"type", "boolean"}, {"description", "Keep the part below the plane, for action=set_keep."}}},
                {"keep_as_parts", {{"type", "boolean"}, {"description", "Keep both halves as parts of one object, for action=set_keep."}}},
                {"shape_kind", {{"type", "integer"}, {"description", "0=circle, 1=square, 2=hexagon, for action=set_shape."}}},
                {"shape_size", {{"type", "number"}, {"description", "Shape size in mm (diameter / side / across-flats), for action=set_shape."}}},
                {"through", {{"type", "boolean"}, {"description", "Cut all the way through (default true); false cuts a blind pocket of 'depth' mm, for action=set_shape."}}},
                {"depth", {{"type", "number"}, {"description", "Blind-pocket depth in mm (used when through is false), for action=set_shape."}}},
                {"offset", {{"type", "number"}, {"description", "Lower-by distance in mm for action=fill_from_above (how far the slab above the plane is dropped into the object). Default keeps the current value."}}},
                {"screen_x", {{"type", "number"}, {"description", "Canvas X for action=pick_face or hover_face (defaults to the viewport centre)."}}},
                {"screen_y", {{"type", "number"}, {"description", "Canvas Y for action=pick_face or hover_face (defaults to the viewport centre)."}}},
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

                GLGizmoCut3D *g = active_cut_gizmo(action == "open");
                if (g == nullptr)
                    return {{"status", "error"},
                            {"error", "Cut gizmo is not active. Load a model and pass object_id or select a single object."}};

                auto need = [&](const char *key) { return params.contains(key); };

                if (action == "set_part") {
                    if (!need("part"))
                        return {{"status", "error"}, {"error", "needs 'part' (model-volume index; -1 = whole object)"}};
                    g->gizmo_set_cut_volume(params["part"].get<int>());
                } else if (action == "open" && need("part")) {
                    g->gizmo_set_cut_volume(params["part"].get<int>());
                } else if (action == "set_plane_normal") {
                    Vec3d n;
                    if (!need("normal") || !json_vec3(params["normal"], n))
                        return {{"status", "error"}, {"error", "needs 'normal': [x,y,z]"}};
                    g->gizmo_set_plane_normal(n);
                } else if (action == "set_plane_center") {
                    Vec3d c;
                    if (!need("center") || !json_vec3(params["center"], c))
                        return {{"status", "error"}, {"error", "needs 'center': [x,y,z]"}};
                    g->gizmo_set_plane_center(c);
                } else if (action == "shift_cut") {
                    if (!need("delta"))
                        return {{"status", "error"}, {"error", "needs 'delta'"}};
                    g->shift_cut(params["delta"].get<double>());
                } else if (action == "set_mode") {
                    if (!need("mode"))
                        return {{"status", "error"}, {"error", "needs 'mode'"}};
                    g->gizmo_set_mode(params["mode"].get<int>());
                } else if (action == "set_keep") {
                    if (need("keep_upper")) g->gizmo_set_keep_upper(params["keep_upper"].get<bool>());
                    if (need("keep_lower")) g->gizmo_set_keep_lower(params["keep_lower"].get<bool>());
                    if (need("keep_as_parts")) g->gizmo_set_keep_as_parts(params["keep_as_parts"].get<bool>());
                    canvas->set_as_dirty();
                } else if (action == "set_shape") {
                    if (!need("shape_kind"))
                        return {{"status", "error"}, {"error", "needs 'shape_kind' (0=circle, 1=square, 2=hexagon)"}};
                    const float size = need("shape_size") ? params["shape_size"].get<float>() : 0.f;
                    g->gizmo_set_shape(params["shape_kind"].get<int>(), size);
                    if (g->gizmo_get_mode() != 2)
                        g->gizmo_set_mode(2);
                    if (need("through"))
                        g->gizmo_set_shape_through(params["through"].get<bool>());
                    if (need("depth"))
                        g->gizmo_set_shape_depth(params["depth"].get<float>());
                } else if (action == "flip") {
                    g->gizmo_flip_plane();
                } else if (action == "reset") {
                    g->gizmo_reset_plane();
                } else if (action == "pick_face") {
                    Vec2d screen;
                    if (need("screen_x") && need("screen_y"))
                        screen = Vec2d(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    else {
                        const std::array<int, 4> viewport = wxGetApp().plater()->get_camera().get_viewport();
                        screen = Vec2d(viewport[0] + 0.5 * viewport[2], viewport[1] + 0.5 * viewport[3]);
                    }
                    if (!g->gizmo_pick_face_at(screen))
                        return {{"status", "error"}, {"error", "No face under the screen point"}};
                } else if (action == "hover_face") {
                    Vec2d screen;
                    if (need("screen_x") && need("screen_y"))
                        screen = Vec2d(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    else {
                        const std::array<int, 4> viewport = wxGetApp().plater()->get_camera().get_viewport();
                        screen = Vec2d(viewport[0] + 0.5 * viewport[2], viewport[1] + 0.5 * viewport[3]);
                    }
                    int   facet = -1, region_facets = 0;
                    Vec3d normal = Vec3d::Zero();
                    if (!g->gizmo_face_info_at(screen, facet, region_facets, normal))
                        return {{"status", "error"}, {"error", "No face under the screen point"}};
                    return {
                        {"status", "ok"},
                        {"action", action},
                        {"facet", facet},
                        {"region_facets", region_facets},
                        {"normal", vec3_json(normal)},
                    };
                } else if (action == "fill_from_above") {
                    if (need("offset"))
                        g->gizmo_set_fill_offset(params["offset"].get<double>());
                    g->gizmo_set_fill_from_above(true);
                    g->gizmo_fill_from_above();
                } else if (action == "apply") {
                    OrcaMCP::McpDialogSuppressionGuard guard;
                    g->gizmo_apply();
                    // The gizmo is closed by perform_cut; report a minimal result.
                    nlohmann::json result = {
                        {"status", "ok"},
                        {"action", action},
                        {"open", false},
                        {"suppressed_dialogs", guard.messages()},
                        {"active_warnings", OrcaMCP::get_active_warnings_json(plater)},
                    };
                    return result;
                } else if (action != "status" && action != "open") {
                    return {{"status", "error"}, {"error", "Unknown action: " + action}};
                }

                nlohmann::json result = cut_gizmo_state(*g, canvas);
                result["status"] = "ok";
                result["open"]   = true;
                result["action"] = action;
                return result;
            });
        }
    });
    register_tool(
         {"edge_dress_gizmo",
          "[ORCAPORT:EF-1] Open and drive the Edge chamfer / fillet tool. Actions: 'open', 'status', "
          "'set_mode' (chamfer|fillet), 'set_size', 'apply' (dress the edge under the cursor), "
          "'apply_at' (screen_x/screen_y), 'hover_at' (screen_x/screen_y), 'list_edges', 'apply_edge' (index), 'remove_edge' (index), "
          "'applied_edges', 'set_loop' (whole rim), 'list_loops' (facet), 'apply_loop' (facet + index), "
          "'remove_loop', 'applied_loops' (facet), 'clear_all', 'close'. An edge or loop that already "
          "carries a feature is not dressed twice; remove it first (right-click in the UI).",
          {{"type", "object"},
           {"properties",
            {{"action",
              {{"type", "string"},
               {"description",
                "open | status | set_mode | set_size | apply | apply_at | hover_at | list_edges | apply_edge | remove_edge | applied_edges | set_loop | list_loops | apply_loop | remove_loop | applied_loops | clear_all | close"}}},
            {"object_id", {{"type", "integer"}, {"description", "Object to select before opening."}}},
            {"mode", {{"type", "string"}, {"description", "chamfer | fillet, for action=set_mode."}}},
            {"size",
             {{"type", "number"},
              {"description", "Chamfer leg / fillet radius in mm, for action=set_size."}}},
            {"screen_x", {{"type", "number"}, {"description", "Canvas X, for action=apply_at / hover_at."}}},
            {"screen_y", {{"type", "number"}, {"description", "Canvas Y, for action=apply_at / hover_at."}}},
            {"max_count",
             {{"type", "integer"}, {"description", "Maximum edges or loops to return, for list_edges / list_loops."}}},
            {"index",
             {{"type", "integer"}, {"description", "Edge index from list_edges, or loop index from list_loops."}}},
            {"loop",
             {{"type", "boolean"}, {"description", "Whole-loop mode on/off, for action=set_loop."}}},
            {"facet",
             {{"type", "integer"},
              {"description", "Patch seed face index, for list_loops / apply_loop. -1 uses the last hovered facet."}}}}},
          {"required", {"action"}}},
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

                GLGizmoEdgeDress *g = active_edge_dress_gizmo(action == "open");
                if (g == nullptr)
                    return {{"status", "error"},
                            {"error", "Edge dress gizmo is not active. Load a model and pass object_id or "
                                      "select a single object."}};

                auto need = [&](const char *key) { return params.contains(key); };

                if (action == "set_mode") {
                    if (!need("mode"))
                        return {{"status", "error"}, {"error", "Missing mode (chamfer|fillet)"}};
                    const std::string m = params["mode"].get<std::string>();
                    if (m == "chamfer")
                        g->set_mode(EdgeDressMode::Chamfer);
                    else if (m == "fillet")
                        g->set_mode(EdgeDressMode::Fillet);
                    else
                        return {{"status", "error"}, {"error", "Unknown mode: " + m}};
                } else if (action == "set_size") {
                    if (!need("size"))
                        return {{"status", "error"}, {"error", "Missing size"}};
                    g->set_size(params["size"].get<double>());
                } else if (action == "set_loop") {
                    if (!need("loop"))
                        return {{"status", "error"}, {"error", "Missing loop (true|false)"}};
                    g->set_loop_mode(params["loop"].get<bool>());
                } else if (action == "apply") {
                    if (!g->gizmo_apply_hovered())
                        return {{"status", "error"}, {"error", "No edge is under the cursor"}};
                } else if (action == "apply_at") {
                    if (!need("screen_x") || !need("screen_y"))
                        return {{"status", "error"}, {"error", "Missing screen_x / screen_y"}};
                    const Vec2d pos(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    if (!g->gizmo_apply_at(pos))
                        return {{"status", "error"}, {"error", "No edge is under that point"}};
                } else if (action == "hover_at") {
                    if (!need("screen_x") || !need("screen_y"))
                        return {{"status", "error"}, {"error", "Missing screen_x / screen_y"}};
                    const Vec2d pos(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    g->gizmo_hover_at(pos);
                } else if (action == "list_edges") {
                    const int max_count = params.value("max_count", 0);
                    nlohmann::json edges = nlohmann::json::array();
                    for (const std::array<double, 6> &e : g->gizmo_list_edges(max_count))
                        edges.push_back({ e[0], e[1], e[2], e[3], e[4], e[5] });
                    nlohmann::json result = edge_dress_gizmo_state(*g);
                    result["status"]    = "ok";
                    result["open"]      = true;
                    result["action"]    = action;
                    result["edge_count"] = g->edge_count();
                    result["edges"]      = std::move(edges);
                    return result;
                } else if (action == "apply_edge") {
                    if (!need("index"))
                        return {{"status", "error"}, {"error", "Missing index"}};
                    if (!g->gizmo_apply_edge(params["index"].get<int>()))
                        return {{"status", "error"},
                                {"error", "Edge could not be dressed: no such index, no valid section there, or a feature is already applied"}};
                } else if (action == "remove_edge") {
                    if (!need("index"))
                        return {{"status", "error"}, {"error", "Missing index"}};
                    if (!g->gizmo_remove_edge(params["index"].get<int>()))
                        return {{"status", "error"}, {"error", "That edge has no applied feature"}};
                } else if (action == "applied_edges") {
                    nlohmann::json hits = nlohmann::json::array();
                    for (const int i : g->applied_edge_indices())
                        hits.push_back(i);
                    nlohmann::json result = edge_dress_gizmo_state(*g);
                    result["status"]  = "ok";
                    result["open"]    = true;
                    result["action"]  = action;
                    result["applied"] = std::move(hits);
                    return result;
                } else if (action == "remove_loop") {
                    if (!need("index"))
                        return {{"status", "error"}, {"error", "Missing index"}};
                    const int facet = params.value("facet", -1);
                    if (!g->gizmo_remove_loop(facet, params["index"].get<int>()))
                        return {{"status", "error"}, {"error", "That loop has no applied feature"}};
                } else if (action == "applied_loops") {
                    const int facet = params.value("facet", -1);
                    nlohmann::json hits = nlohmann::json::array();
                    for (const std::array<int, 2> &h : g->applied_loop_indices(facet))
                        hits.push_back({{"facet", h[0]}, {"index", h[1]}});
                    nlohmann::json result = edge_dress_gizmo_state(*g);
                    result["status"]  = "ok";
                    result["open"]    = true;
                    result["action"]  = action;
                    result["applied"] = std::move(hits);
                    return result;
                } else if (action == "list_loops") {
                    const int facet     = params.value("facet", -1);
                    const int max_count = params.value("max_count", 0);
                    nlohmann::json loops = nlohmann::json::array();
                    for (const std::vector<std::array<double, 3>> &loop : g->gizmo_list_loops(facet, max_count)) {
                        nlohmann::json pts = nlohmann::json::array();
                        for (const std::array<double, 3> &p : loop)
                            pts.push_back({ p[0], p[1], p[2] });
                        loops.push_back(std::move(pts));
                    }
                    nlohmann::json result = edge_dress_gizmo_state(*g);
                    result["status"]     = "ok";
                    result["open"]       = true;
                    result["action"]     = action;
                    result["facet"]      = facet >= 0 ? facet : g->loop_facet();
                    result["loop_count"] = g->loop_count(facet);
                    result["loops"]      = std::move(loops);
                    return result;
                } else if (action == "apply_loop") {
                    if (!need("index"))
                        return {{"status", "error"}, {"error", "Missing index"}};
                    const int facet = params.value("facet", -1);
                    if (!g->gizmo_apply_loop(facet, params["index"].get<int>()))
                        return {{"status", "error"},
                                {"error", "Loop could not be dressed: no such index, no valid section there, or a feature is already applied"}};
                } else if (action == "clear_all") {
                    g->gizmo_clear_all();
                } else if (action != "status" && action != "open") {
                    return {{"status", "error"}, {"error", "Unknown action: " + action}};
                }

                nlohmann::json result = edge_dress_gizmo_state(*g);
                result["status"] = "ok";
                result["open"] = true;
                result["action"] = action;
                return result;
            });
        }
    });
    register_tool(
        {"extrude_gizmo",
         "[ORCAPORT:HF-1] [ORCAPORT:HF-3] Open and drive the Extrude tool. Two modes: extrude_face "
         "(default) turns a picked flat face into a solid; cavity_fill plugs a depression (engraving, "
         "watermark, pocket) with a positive volume built from the recessed faces projected up to the "
         "surface fitted around them. Actions: "
         "'open', 'status', 'set_mode' (extrude_face | cavity_fill), 'set_extrude' (height / offset / "
         "holes / both), 'pick_at' (screen_x/screen_y - click a face to SELECT it), 'clear_pick', "
         "'create' (commit the picked face as a solid), 'set_radius' (cavity brush width, mm), "
         "'hover_at' (screen_x/screen_y), 'apply_at' (screen_x/screen_y - pick then create, one call), "
         "'apply' (same, for the face already under the cursor), "
         "'list_fills' (volume index + object-space centre of each volume added), 'remove_fill' "
         "(index), 'clear_all', 'refresh', 'close'. In extrude_face mode nothing is added until "
         "'create': pick first, then dial the height and offset against the preview.",
         {{"type", "object"},
          {"properties",
           {{"action",
             {{"type", "string"},
              {"description",
               "open | status | set_mode | set_extrude | pick_at | clear_pick | create | set_radius | hover_at | apply_at | apply | list_fills | remove_fill | clear_all | refresh | close"}}},
            {"object_id", {{"type", "integer"}, {"description", "Object to select before opening."}}},
            {"mode", {{"type", "string"}, {"description", "extrude_face | cavity_fill, for action=set_mode."}}},
            {"height", {{"type", "number"}, {"description", "Extrusion height in mm, for action=set_extrude."}}},
            {"offset", {{"type", "number"}, {"description", "Outline offset in mm (negative shrinks), for action=set_extrude."}}},
            {"holes", {{"type", "boolean"}, {"description", "Extrude only the face's holes, for action=set_extrude."}}},
            {"both", {{"type", "boolean"}, {"description", "Half out of the face, half into the model, for action=set_extrude."}}},
            {"radius", {{"type", "number"}, {"description", "Brush width in mm, for action=set_radius."}}},
            {"screen_x", {{"type", "number"}, {"description", "Canvas X, for action=pick_at / apply_at / hover_at."}}},
            {"screen_y", {{"type", "number"}, {"description", "Canvas Y, for action=pick_at / apply_at / hover_at."}}},
            {"index",
             {{"type", "integer"}, {"description", "Fill volume index from list_fills, for action=remove_fill."}}}}},
          {"required", {"action"}}},
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

                GLGizmoExtrude *g = active_extrude_gizmo(action == "open");
                if (g == nullptr)
                    return {{"status", "error"},
                            {"error", "Cavity fill gizmo is not active. Load a model and pass object_id or "
                                      "select a single object."}};

                auto need = [&](const char *key) { return params.contains(key); };

                if (action == "set_radius") {
                    if (!need("radius"))
                        return {{"status", "error"}, {"error", "Missing radius"}};
                    g->set_radius(params["radius"].get<double>());
                } else if (action == "set_mode") {
                    if (!need("mode"))
                        return {{"status", "error"}, {"error", "Missing mode (extrude_face | cavity_fill)"}};
                    const std::string m = params["mode"].get<std::string>();
                    if (m == "extrude_face" || m == "extrude")
                        g->set_mode(GLGizmoExtrude::Mode::ExtrudeFace);
                    else if (m == "cavity_fill" || m == "cavity")
                        g->set_mode(GLGizmoExtrude::Mode::CavityFill);
                    else
                        return {{"status", "error"}, {"error", "Unknown mode: " + m}};
                } else if (action == "set_extrude") {
                    // [ORCAPORT:HF-3] The extrude-face parameters.
                    if (need("height"))
                        g->set_extrude_height(params["height"].get<double>());
                    if (need("offset"))
                        g->set_extrude_offset(params["offset"].get<double>());
                    if (need("holes"))
                        g->set_extrude_holes(params["holes"].get<bool>());
                    if (need("both"))
                        g->set_extrude_both(params["both"].get<bool>());
                } else if (action == "pick_at") {
                    if (!need("screen_x") || !need("screen_y"))
                        return {{"status", "error"}, {"error", "Missing screen_x / screen_y"}};
                    const Vec2d pos(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    if (!g->pick_at(pos))
                        return {{"status", "error"},
                                {"error", "No flat face at that point (nothing under the cursor, or the face has no usable outline)."}};
                } else if (action == "clear_pick") {
                    g->clear_pick();
                } else if (action == "create") {
                    // [ORCAPORT:HF-3] Commit the picked face as a solid.
                    if (!g->create_from_pick())
                        return {{"status", "error"},
                                {"error", "Nothing to extrude (no face picked, or the offset removes the shape)."}};
                } else if (action == "hover_at") {
                    if (!need("screen_x") || !need("screen_y"))
                        return {{"status", "error"}, {"error", "Missing screen_x / screen_y"}};
                    const Vec2d pos(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    g->gizmo_hover_at(pos);
                } else if (action == "apply_at") {
                    if (!need("screen_x") || !need("screen_y"))
                        return {{"status", "error"}, {"error", "Missing screen_x / screen_y"}};
                    const Vec2d pos(params["screen_x"].get<double>(), params["screen_y"].get<double>());
                    if (!g->gizmo_apply_at(pos))
                        return {{"status", "error"},
                                {"error", "Nothing to add at that point (nothing under the cursor, the face is already covered, or the offset removes the shape)."}};
                } else if (action == "apply") {
                    if (!g->gizmo_apply_hovered())
                        return {{"status", "error"},
                                {"error", "Nothing to add under the cursor (hover first, or the face is already covered / the offset removes the shape)."}};
                } else if (action == "list_fills") {
                    nlohmann::json fills = nlohmann::json::array();
                    const int      oi    = canvas->get_selection().get_object_idx();
                    if (oi >= 0 && oi < int(plater->model().objects.size())) {
                        const ModelObject *mo = plater->model().objects[oi];
                        for (size_t i = 0; i < mo->volumes.size(); ++i) {
                            const ModelVolume *v = mo->volumes[i];
                            // Accept the current prefix and the legacy one, so fills created before
                            // the tool was renamed to Extrude can still be listed and removed.
                            if (v == nullptr || (v->name.rfind("Extrude#", 0) != 0 && v->name.rfind("HoleFill#", 0) != 0)
                                || v->mesh().its.vertices.empty())
                                continue;
                            Vec3f c = Vec3f::Zero();
                            for (const stl_vertex &p : v->mesh().its.vertices)
                                c += p;
                            c /= float(v->mesh().its.vertices.size());
                            const Vec3d center = v->get_matrix() * c.cast<double>();
                            fills.push_back({{"index", int(i)}, {"center", {center.x(), center.y(), center.z()}}});
                        }
                    }
                    nlohmann::json result = extrude_gizmo_state(*g);
                    result["status"] = "ok";
                    result["open"]   = true;
                    result["action"] = action;
                    result["fills"]  = std::move(fills);
                    return result;
                } else if (action == "remove_fill") {
                    if (!need("index"))
                        return {{"status", "error"}, {"error", "Missing index"}};
                    if (!g->gizmo_remove_fill(params["index"].get<int>()))
                        return {{"status", "error"}, {"error", "That volume is not a cavity fill"}};
                } else if (action == "clear_all") {
                    g->gizmo_clear_all();
                } else if (action == "refresh") {
                    g->gizmo_refresh();
                } else if (action != "status" && action != "open") {
                    return {{"status", "error"}, {"error", "Unknown action: " + action}};
                }

                nlohmann::json result = extrude_gizmo_state(*g);
                result["status"] = "ok";
                result["open"] = true;
                result["action"] = action;
                return result;
            });
        }
    });

    // [ORCAPORT:UI-2] The screenshot tool. The gizmo panels are ImGui drawn into the GL canvas, so
    // nothing else can see them - not the 3D-view renderers, not the window geometry. A UI bug (a
    // panel that does not fit, a row that is clipped, an icon at the wrong size, an empty reserved
    // block) is only visible in a picture of the window.
    register_tool(
        {"ui_screenshot",
         "Screenshot a gizmo panel, or the whole window, as a PNG. The panels are ImGui drawn into "
         "the GL canvas, so this is the ONLY way to see one. By default it opens the named gizmo, "
         "crops to that panel and upscales it 2x, so the image is the panel and nothing else - what "
         "you want for checking a row, a label column, an icon or a switch. Pass region='window' when "
         "WHERE the panel sits is the question (anchoring, clipping, overlap). gizmo: 'perforation', "
         "'holes', 'edge_dress', 'hole_walls', 'support_zones', 'align_stack', 'extrude', 'cut', "
         "'seam', 'fuzzy_skin', 'move', ... or 'close' for none. Use save_to_file=true (the default) "
         "and read the returned path. The window must be visible: raise=true (default) brings it to "
         "the front first, since the capture is a screen blit.",
         {{"type", "object"},
          {"properties",
           {{"gizmo",
             {{"type", "string"},
              {"description",
               "Gizmo to open before capturing ('perforation', 'holes', 'edge_dress', 'hole_walls', "
               "'support_zones', 'align_stack', 'extrude', 'cut', 'seam', 'fuzzy_skin', 'move', ...). "
               "Omit to capture whatever is open; 'close' closes every gizmo first."}}},
            {"region", {{"type", "string"}, {"description", "'gizmo' (default) crops to the open gizmo panel; 'window' returns the whole window."}}},
            {"scale", {{"type", "integer"}, {"description", "Upscale factor, default 2. Use 1 for pixel-exact."}}},
            {"object_id", {{"type", "integer"}, {"description", "Object to select before opening a gizmo."}}},
            {"raise", {{"type", "boolean"}, {"description", "Bring the window to the front first (default true)."}}},
            {"save_to_file", {{"type", "boolean"}, {"description", "Save a PNG and return its path (default true)."}}},
            {"file_path", {{"type", "string"}, {"description", "Where to write the PNG. Defaults to the temp dir with a timestamp."}}}}},
          {"required", nlohmann::json::array()}},
        [](const nlohmann::json &params) -> nlohmann::json {
            const std::string gizmo      = params.value("gizmo", std::string());
            const std::string file_path  = params.value("file_path", std::string());
            const std::string region     = params.value("region", std::string("gizmo"));
            const int         scale      = params.value("scale", 2);
            const bool        raise      = params.value("raise", true);
            const bool        to_file    = params.value("save_to_file", true);
            const int         object_id  = params.contains("object_id") ? params["object_id"].get<int>() : -1;
            return run_on_main_thread([=]() -> nlohmann::json {
                Plater     *plater = wxGetApp().plater();
                GLCanvas3D *canvas = plater != nullptr ? plater->get_view3D_canvas3D() : nullptr;
                if (canvas == nullptr)
                    return {{"status", "error"}, {"error", "No 3D canvas"}};

                std::string opened;
                if (! gizmo.empty()) {
                    if (gizmo == "none" || gizmo == "close") {
                        canvas->reset_all_gizmos();
                        opened = "close";
                    } else {
                        const GLGizmosManager::EType type = gizmo_type_from_name(gizmo);
                        if (type == GLGizmosManager::EType::Undefined)
                            return {{"status", "error"}, {"error", "Unknown gizmo '" + gizmo + "'"}};
                        if (object_id >= 0)
                            select_object_for_gizmo(object_id);
                        canvas->get_gizmos_manager().open_gizmo(type);
                        opened = gizmo;
                    }
                    canvas->set_as_dirty();
                    canvas->request_extra_frame();
                }

                wxWindow *win = wxGetApp().GetTopWindow();
                if (win == nullptr)
                    return {{"status", "error"}, {"error", "No top-level window"}};

                if (raise) {
                    win->Raise();
                    win->SetFocus();
                }
                // 🔑 Drive the canvas until the panel's rect SETTLES, not just once. Three reasons,
                // each of which alone makes a single pass useless:
                //   - the panel is laid out during the canvas's paint, and the canvas only paints from
                //     the app's idle cycle, which cannot run while this call is inside the event
                //     handler, so nothing may be laid out at all;
                //   - ImGui needs one pass to give an auto-sized window its size - GLGizmoBase says so
                //     and forces a second frame itself ("not rendered in the first frame"), so the
                //     first pass reports a panel a single row tall;
                //   - render() ends in SwapBuffers(), so the frame is also on screen for the blit.
                canvas->set_as_dirty();
                GLGizmoBase::PanelRect prev{};
                for (int i = 0; i < 5; ++i) {
                    canvas->render();
                    GLGizmoBase *cur = canvas->get_gizmos_manager().get_current();
                    const GLGizmoBase::PanelRect now = cur != nullptr ? cur->last_input_window_rect()
                                                                       : GLGizmoBase::PanelRect{};
                    if (now.h > 1.f && now.w == prev.w && now.h == prev.h)
                        break; // stable across two frames: this is the laid-out panel
                    prev = now;
                }

                std::vector<unsigned char> png;
                std::string                err;
                int                        w = 0, h = 0;
                if (! capture_top_window_png(png, w, h, err))
                    return {{"status", "error"}, {"error", err}};

                nlohmann::json result = {
                    {"status", "ok"},
                    {"gizmo", opened},
                    {"window_width", w},
                    {"window_height", h},
                };

                wxImage img;
                {
                    wxMemoryInputStream in(png.data(), png.size());
                    if (! img.LoadFile(in, wxBITMAP_TYPE_PNG))
                        return {{"status", "error"}, {"error", "The capture could not be decoded"}};
                }

                // Crop to the panel. A picture of the whole app is mostly app: the point of this tool
                // is to look at ONE panel with nothing else in the frame. `region=window` is there for
                // the opposite case, when where the panel sits IS the question (a panel that opens off
                // the canvas edge, or anchored under the wrong icon).
                if (region != "window") {
                    wxRect panel;
                    if (! gizmo_panel_screen_rect(canvas, panel, err))
                        return {{"status", "error"}, {"error", err}};
                    const wxRect win_rect = win->GetScreenRect();
                    panel.Offset(-win_rect.x, -win_rect.y); // window-relative: the capture's origin
                    result["panel"] = {{"x", panel.x}, {"y", panel.y}, {"w", panel.width}, {"h", panel.height}};

                    constexpr int MARGIN = 6; // keep the panel's rounding and its drop shadow
                    wxRect crop(panel.x - MARGIN, panel.y - MARGIN,
                                panel.width + 2 * MARGIN, panel.height + 2 * MARGIN);
                    crop.Intersect(wxRect(0, 0, w, h));
                    if (crop.width < 8 || crop.height < 8)
                        return {{"status", "error"}, {"error", "The panel rect is off screen"},
                                {"panel", result["panel"]}};
                    img = img.GetSubImage(crop);
                }

                // Upscale: the panels are ~360 px wide and the point is reading the widgets.
                if (scale > 1)
                    img = img.Scale(img.GetWidth() * scale, img.GetHeight() * scale, wxIMAGE_QUALITY_NORMAL);

                {
                    wxMemoryOutputStream stream;
                    if (! img.SaveFile(stream, wxBITMAP_TYPE_PNG))
                        return {{"status", "error"}, {"error", "PNG encode failed"}};
                    wxStreamBuffer *buf = stream.GetOutputStreamBuffer();
                    png.assign(static_cast<unsigned char *>(buf->GetBufferStart()),
                               static_cast<unsigned char *>(buf->GetBufferStart()) + buf->GetBufferSize());
                }
                result["width"]  = img.GetWidth();
                result["height"] = img.GetHeight();
                result["bytes"]  = int(png.size());

                if (to_file) {
                    std::string path = file_path;
                    if (path.empty()) {
                        const std::string stamp = wxDateTime::Now().Format("%Y%m%d-%H%M%S").ToStdString();
                        const std::string what  = opened.empty() ? std::string("ui") : opened;
                        path = (wxStandardPaths::Get().GetTempDir() + "/orca-panel-" + what.c_str() + "-" +
                                stamp.c_str() + ".png")
                                   .ToStdString();
                    }
                    wxFileOutputStream out(wxString::FromUTF8(path));
                    if (! out.IsOk())
                        return {{"status", "error"}, {"error", "Cannot write " + path}};
                    out.Write(png.data(), png.size());
                    result["path"] = path;
                } else {
                    result["image_base64"] = "data:image/png;base64," +
                        std::string(wxBase64Encode(png.data(), png.size()).ToStdString());
                }
                return result;
            });
        }
    });
}

}} // namespace Slic3r::GUI
