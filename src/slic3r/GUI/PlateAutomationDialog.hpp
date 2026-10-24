// [ORCAPORT FILE] PlateAutomationDialog - edit the local per-plate-type first-layer flow defaults
// Source: original (design in porting/notes/PA-1.md)
//
// One popup for the flow automation the upload dialog offers: a reduction of the first layer's bottom
// surface flow, per plate type, saved on this computer (AppConfig) and never in a preset or a 3mf.
#ifndef slic3r_GUI_PlateAutomationDialog_hpp_
#define slic3r_GUI_PlateAutomationDialog_hpp_

#include <functional>
#include <vector>

#include <wx/dialog.h>
#include <wx/stattext.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/OrcaExt/PlateAutomation.hpp"
#include "GUI_Utils.hpp"

class Button;
class TextInput;

namespace Slic3r {
namespace GUI {

class RadioBox;

class PlateAutomationDialog : public DPIDialog
{
public:
    // `bed_type` is preselected; `on_applied` runs after every Apply so the caller can refresh its
    // row while the dialog stays open.
    PlateAutomationDialog(wxWindow *parent, BedType bed_type, std::function<void()> on_applied);
    ~PlateAutomationDialog() override = default;

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    struct Tile {
        Button       *button {nullptr};
        wxStaticText *value {nullptr};
        BedType       bed_type {BedType::btDefault};
        bool          selected {false};
    };

    void create_plate_tiles(wxSizer *sizer);
    void toggle_tile(size_t index);
    void load_controls_from(BedType bed_type);
    void refresh_tile_values();
    void refresh_flow_controls(double flow_percent);
    void set_hint(const wxString &text);
    bool read_flow_control(double &flow_percent) const;
    void apply();

    std::vector<Tile> m_tiles;
    std::vector<size_t> m_selected;
    size_t            m_focus {0};
    std::vector<RadioBox *> m_flow_radios;
    TextInput        *m_flow_custom {nullptr};
    wxStaticText     *m_hint {nullptr};
    std::function<void()> m_on_applied;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PlateAutomationDialog_hpp_
