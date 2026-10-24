// [ORCAPORT FILE] PlateAutomationDialog - edit the local per-plate automation
// Source: original (design in porting/notes/PA-1.md)
//
// One popup for both automation knobs the upload dialog has: the first-layer flow reduction and the
// bed-temperature offset chosen from the largest part's size bucket. Both are per plate type and are
// saved on this computer (AppConfig), never in a preset or a 3mf.
#ifndef slic3r_GUI_PlateAutomationDialog_hpp_
#define slic3r_GUI_PlateAutomationDialog_hpp_

#include <array>
#include <functional>
#include <vector>

#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/stattext.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/OrcaExt/PlateAutomation.hpp"
#include "GUI_Utils.hpp"

class Button;
class SpinInput;
class TextInput;

namespace Slic3r {
namespace GUI {

class RadioBox;

// Short, user-facing name of a part size bucket ("Small", "Small-medium", ...). Shared by this
// dialog's rows and the send dialog's status line so the two can never disagree.
wxString plate_bucket_short_name(OrcaExt::PartSizeBucket bucket);

class PlateAutomationDialog : public DPIDialog
{
public:
    // `bed_type` is preselected; `on_applied` runs after every Apply so the caller can refresh its
    // rows while the dialog stays open.
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
    void create_band_controls(wxSizer *sizer);
    void create_band_matrix(wxSizer *sizer);
    void toggle_band_matrix();
    void toggle_tile(size_t index);
    void load_controls_from(BedType bed_type);
    void refresh_tile_values();
    void refresh_flow_controls(double flow_percent);
    bool read_band_controls(OrcaExt::PartSizeLimits &limits) const;
    void load_band_controls(const OrcaExt::PartSizeLimits &limits);
    void refresh_bands_feedback();
    void save_bands();
    void set_hint(const wxString &text);
    bool read_flow_control(double &flow_percent) const;
    void apply();

    std::vector<Tile> m_tiles;
    std::vector<size_t> m_selected;
    size_t            m_focus {0};
    std::vector<RadioBox *> m_flow_radios;
    TextInput        *m_flow_custom {nullptr};
    std::array<SpinInput *, OrcaExt::kPartSizeBucketCount> m_bucket_inputs {};
    std::array<TextInput *, OrcaExt::kPartSizeFootprintCols - 1> m_footprint_inputs {};
    std::array<TextInput *, OrcaExt::kPartSizeHeightRows - 1>    m_height_inputs {};
    wxStaticText     *m_bands_caption {nullptr};
    wxStaticText     *m_footprint_ranges {nullptr};
    wxStaticText     *m_height_ranges {nullptr};
    wxStaticText     *m_override_note {nullptr};
    wxStaticText     *m_bands_readout {nullptr};
    wxPanel          *m_matrix_panel {nullptr};
    Button           *m_matrix_toggle {nullptr};
    wxStaticText     *m_matrix_col_labels[OrcaExt::kPartSizeFootprintCols] {};
    wxStaticText     *m_matrix_row_labels[OrcaExt::kPartSizeHeightRows] {};
    wxStaticText     *m_matrix_cells[OrcaExt::kPartSizeHeightRows][OrcaExt::kPartSizeFootprintCols] {};
    std::vector<std::pair<double, double>> m_plate_footprints;
    double            m_plate_area_mm2 {0.};
    wxStaticText     *m_hint {nullptr};
    std::function<void()> m_on_applied;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PlateAutomationDialog_hpp_
