// [ORCAPORT FILE] OrcaExtGuiPreviewClipController - interactive clipping plane for the G-code preview
// Source: preFlight v1.3.0 (preFlight.PreviewClipController.*); re-implemented on OrcaSlicer 2.5 APIs
#ifndef slic3r_OrcaExt_PreviewClipController_hpp_
#define slic3r_OrcaExt_PreviewClipController_hpp_

#include "libslic3r/BoundingBox.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {
class GCodeViewer;
class GLCanvas3D;
class Camera;
} // namespace GUI

namespace OrcaExt {
namespace Gui {

// Interactive clipping plane for the G-code preview: right-click an object in Preview and
// activate it to cut both the toolpaths and the shell with a movable plane.
class PreviewClipController
{
public:
    void activate(int object_id);
    void deactivate();
    void set_position(double ratio);
    void reset_direction();
    void render_imgui();

    bool is_active() const { return m_active; }
    int  get_object_id() const { return m_object_id; }

private:
    void        apply_clipping_plane();
    std::string get_object_name() const;

    bool m_active    = false;
    int  m_object_id = -1;

    Vec3d        m_clip_normal{ 0.0, 0.0, 1.0 };
    double       m_clip_ratio = 0.5;
    BoundingBoxf3 m_object_bbox;

    struct SavedState
    {
        std::vector<bool> shell_visibility;
        bool              shells_visible{ false };
    };
    std::optional<SavedState> m_saved_state;
};

} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r

#endif // slic3r_OrcaExt_PreviewClipController_hpp_
