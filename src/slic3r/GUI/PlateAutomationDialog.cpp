#include "PlateAutomationDialog.hpp"

#include <algorithm>
#include <cmath>

#include <wx/sizer.h>
#include <wx/statbmp.h>

#include "BedTypeThumbnails.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "I18N.hpp"
#include "PlateAutomationStore.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/StateColor.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

namespace {

// The plate types the automation covers, in the order the plate picker shows them (all of them have
// a picture, see BedTypeThumbnails.hpp).
const std::array<BedType, 6> kAutomationPlates {btPC, btEP, btPEI, btPTE, btPCT, btSuperTack};

// Flow choices; 0 is "none" (print at the profile's flow). A custom value outside this grid can be
// typed into the field.
const std::array<double, 5> kFlowChoices {-20., -15., -10., -5., 0.};

// Bucket rows: short name plus a tooltip with the rule that lands a part in it.
wxString plate_name(BedType bed_type)
{
    const ConfigOptionDef *def = print_config_def.get("curr_bed_type");
    const int              idx = int(bed_type) - 1;
    return (def != nullptr && idx >= 0 && idx < int(def->enum_labels.size())) ? from_u8(def->enum_labels[idx]) : _L("Unknown plate");
}

wxString number_text(double value) { return wxString::Format("%g", value); }

// One matrix column header: "< 3%", "3-25%", "> 75%".
wxString footprint_column_text(const OrcaExt::PartSizeLimits &limits, int column)
{
    if (column <= 0)
        return wxString::Format(_L("< %s%%"), number_text(limits.fp_pct.front()));
    if (column >= OrcaExt::kPartSizeFootprintCols - 1)
        return wxString::Format(_L("> %s%%"), number_text(limits.fp_pct.back()));
    return wxString::Format(_L("%s-%s%%"), number_text(limits.fp_pct[column - 1]), number_text(limits.fp_pct[column]));
}

// One matrix row header: "< 1", "1-3", ">= 100".
wxString height_row_text(const OrcaExt::PartSizeLimits &limits, int row)
{
    if (row <= 0)
        return wxString::Format(_L("< %s"), number_text(limits.h_mm.front()));
    if (row >= OrcaExt::kPartSizeHeightRows - 1)
        return wxString::Format(_L(">= %s"), number_text(limits.h_mm.back()));
    return wxString::Format(_L("%s-%s"), number_text(limits.h_mm[row - 1]), number_text(limits.h_mm[row]));
}

// The caption under the fields: every band the numbers currently define.
wxString footprint_ranges_line(const OrcaExt::PartSizeLimits &limits)
{
    wxString text;
    for (int column = 0; column < OrcaExt::kPartSizeFootprintCols; ++column)
        text += (column > 0 ? "  \u00B7  " : "") + footprint_column_text(limits, column);
    return text;
}

wxString height_ranges_line(const OrcaExt::PartSizeLimits &limits)
{
    wxString text;
    for (int row = 0; row < OrcaExt::kPartSizeHeightRows; ++row)
        text += (row > 0 ? "  \u00B7  " : "") + height_row_text(limits, row);
    return text;
}

} // namespace

wxString plate_bucket_short_name(OrcaExt::PartSizeBucket bucket)
{
    using B = OrcaExt::PartSizeBucket;
    switch (bucket) {
        case B::Small:       return _L("Small");
        case B::SmallMedium: return _L("Small-medium");
        case B::Medium:      return _L("Medium");
        case B::MediumLarge: return _L("Medium-large");
        case B::Large:       return _L("Large");
        case B::Tall:        return _L("Tall");
        default:             return wxString();
    }
}

PlateAutomationDialog::PlateAutomationDialog(wxWindow *parent, BedType bed_type, std::function<void()> on_applied)
    : DPIDialog(parent, wxID_ANY, _L("Plate automation"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_on_applied(std::move(on_applied))
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);

    // The plate being uploaded, for the bands readout: its instances stay put while the dialog is
    // open, so measure them once.
    m_plate_footprints.clear();
    m_plate_area_mm2 = 0.;
    if (wxGetApp().plater() != nullptr)
        if (PartPlate *plate = wxGetApp().plater()->get_partplate_list().get_curr_plate()) {
            m_plate_footprints = plate->get_instance_footprints();
            m_plate_area_mm2   = OrcaExt::plate_area_mm2(plate->get_size());
        }

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    auto *explain = new wxStaticText(this, wxID_ANY,
        _L("Saved on this computer only - no project, profile or downloaded 3mf changes them. A value chosen in the print dialog applies to that project only."));
    explain->SetFont(::Label::Body_12);
    explain->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    explain->Wrap(FromDIP(520));
    main_sizer->Add(explain, 0, wxALL | wxEXPAND, FromDIP(12));

    auto *select_hint = new wxStaticText(this, wxID_ANY, _L("Select the plates to edit, then Apply."));
    select_hint->SetFont(::Label::Body_12);
    select_hint->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    main_sizer->Add(select_hint, 0, wxLEFT | wxRIGHT, FromDIP(12));

    auto *tiles = new wxFlexGridSizer(2, 3, FromDIP(8), FromDIP(8));
    create_plate_tiles(tiles);
    main_sizer->Add(tiles, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, FromDIP(12));

    // First layer flow: the four steps the send dialog offers plus None, and a free-form field.
    auto *flow_caption = new wxStaticText(this, wxID_ANY, _L("First layer flow"));
    flow_caption->SetFont(::Label::Body_13);
    flow_caption->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    main_sizer->Add(flow_caption, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto *flow_row = new wxBoxSizer(wxHORIZONTAL);
    for (double choice : kFlowChoices) {
        auto *radio = new RadioBox(this);
        radio->Bind(wxEVT_LEFT_DOWN, [this, choice](wxMouseEvent &) {
            // A step choice is exclusive with the custom field.
            if (m_flow_custom != nullptr)
                m_flow_custom->GetTextCtrl()->ChangeValue(wxEmptyString);
            for (size_t i = 0; i < m_flow_radios.size(); ++i)
                m_flow_radios[i]->SetValue(kFlowChoices[i] == choice);
        });
        flow_row->Add(radio, 0, wxALL | wxALIGN_CENTER, FromDIP(2));
        m_flow_radios.push_back(radio);

        auto *text = new wxStaticText(this, wxID_ANY,
            choice == 0. ? _L("None") : wxString::Format("%d%%", int(choice)));
        text->SetFont(::Label::Body_13);
        text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
        flow_row->Add(text, 0, wxALL | wxALIGN_CENTER, FromDIP(2));
        flow_row->AddSpacer(FromDIP(6));
    }
    m_flow_custom = new TextInput(this, wxEmptyString, "%", "", wxDefaultPosition, FromDIP(wxSize(90, 30)), wxTE_CENTRE);
    flow_row->Add(m_flow_custom, 0, wxALL | wxALIGN_CENTER, FromDIP(2));
    main_sizer->Add(flow_row, 0, wxLEFT | wxRIGHT, FromDIP(12));

    // Bed temperature: one offset per part size bucket, the way the send dialog adds it to the
    // plate temperature.
    auto *bed_caption = new wxStaticText(this, wxID_ANY, _L("Bed temperature offset by part size"));
    bed_caption->SetFont(::Label::Body_13);
    bed_caption->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    main_sizer->Add(bed_caption, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto *bucket_grid = new wxFlexGridSizer(OrcaExt::kPartSizeBucketCount, 2, FromDIP(4), FromDIP(10));
    for (int i = 0; i < OrcaExt::kPartSizeBucketCount; ++i) {
        auto *label = new wxStaticText(this, wxID_ANY, plate_bucket_short_name(OrcaExt::PartSizeBucket(i)));
        label->SetFont(::Label::Body_13);
        label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
        bucket_grid->Add(label, 0, wxALIGN_CENTER_VERTICAL);

        m_bucket_inputs[i] = new SpinInput(this, wxEmptyString, _L("\u00B0C" /* degC */), wxDefaultPosition,
                                          FromDIP(wxSize(90, 30)), wxTE_CENTRE,
                                          OrcaExt::kBedTempOffsetMin, OrcaExt::kBedTempOffsetMax, 0, 5);
        bucket_grid->Add(m_bucket_inputs[i], 0, wxALIGN_CENTER_VERTICAL);
    }
    main_sizer->Add(bucket_grid, 0, wxALL, FromDIP(12));

    // Size bands: the six edges behind the buckets, saved locally so they can be tuned without a
    // rebuild. They apply to every plate type, unlike the offsets above.
    create_band_controls(main_sizer);

    m_hint = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_hint->SetFont(::Label::Body_12);
    m_hint->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    main_sizer->Add(m_hint, 0, wxLEFT | wxRIGHT, FromDIP(12));

    auto *button_row = new wxBoxSizer(wxHORIZONTAL);
    button_row->AddStretchSpacer();
    auto *apply_button = new Button(this, _L("Apply"));
    apply_button->SetStyle(ButtonStyle::Confirm, ButtonType::Window);
    apply_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply(); });
    button_row->Add(apply_button, 0, wxALL, FromDIP(6));

    auto *close_button = new Button(this, _L("Close"));
    close_button->SetStyle(ButtonStyle::Regular, ButtonType::Window);
    close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });
    button_row->Add(close_button, 0, wxALL, FromDIP(6));
    main_sizer->Add(button_row, 0, wxEXPAND | wxALL, FromDIP(6));

    SetSizerAndFit(main_sizer);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    // Preselect the plate being uploaded and show its values.
    const auto it = std::find(kAutomationPlates.begin(), kAutomationPlates.end(), bed_type);
    m_focus       = it == kAutomationPlates.end() ? 0 : size_t(it - kAutomationPlates.begin());
    toggle_tile(m_focus);
    refresh_tile_values();

    load_band_controls(load_plate_bucket_limits(*wxGetApp().app_config));
    refresh_bands_feedback();
}

void PlateAutomationDialog::create_plate_tiles(wxSizer *sizer)
{
    for (BedType bed_type : kAutomationPlates) {
        auto *cell = new wxBoxSizer(wxVERTICAL);

        const std::string image = bed_type_thumbnail_name(bed_type);
        auto *button = new Button(this, plate_name(bed_type), wxString::FromUTF8(image), 0, FromDIP(48));
        button->SetVertical(true);
        button->SetMinSize(FromDIP(wxSize(160, 96)));

        Tile tile;
        tile.button   = button;
        tile.bed_type = bed_type;
        m_tiles.push_back(tile);

        const size_t index = m_tiles.size() - 1;
        button->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent &) { toggle_tile(index); });
        cell->Add(button, 0, wxALIGN_CENTER_HORIZONTAL);

        auto *value = new wxStaticText(this, wxID_ANY, wxEmptyString);
        value->SetFont(::Label::Body_12);
        value->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
        m_tiles.back().value = value;
        cell->Add(value, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(2));

        sizer->Add(cell, 0, wxALIGN_CENTER_HORIZONTAL);
    }
}

void PlateAutomationDialog::create_band_controls(wxSizer *sizer)
{
    auto make_label = [this](wxSizer *row, const wxString &text) {
        auto *item = new wxStaticText(this, wxID_ANY, text);
        item->SetFont(::Label::Body_12);
        item->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
        row->Add(item, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    };
    auto make_input = [this]() {
        auto *field = new TextInput(this, wxEmptyString, wxEmptyString, "", wxDefaultPosition, FromDIP(wxSize(64, 30)), wxTE_CENTRE);
        // Tuning loop: the ranges, the table headers and the readout follow what is typed, before
        // anything is saved.
        field->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
            refresh_bands_feedback();
            event.Skip();
        });
        return field;
    };
    auto make_note = [this](wxSizer *target, wxStaticText *&label) {
        label = new wxStaticText(this, wxID_ANY, wxEmptyString);
        label->SetFont(::Label::Body_12);
        label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#5A6A6E")));
        target->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    };

    // Header: caption, the bands table (hidden by default) and the built-in values.
    auto *header_row = new wxBoxSizer(wxHORIZONTAL);
    m_bands_caption = new wxStaticText(this, wxID_ANY, _L("Size bands (all plates)"));
    m_bands_caption->SetFont(::Label::Body_13);
    m_bands_caption->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    header_row->Add(m_bands_caption, 0, wxALIGN_CENTER_VERTICAL);
    header_row->AddStretchSpacer();

    m_matrix_toggle = new Button(this, _L("Show table"));
    m_matrix_toggle->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    m_matrix_toggle->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { toggle_band_matrix(); });
    header_row->Add(m_matrix_toggle, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    auto *defaults = new Button(this, _L("Defaults"));
    defaults->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    defaults->SetToolTip(_L("Fill in the built-in bands; press Save bands to keep them."));
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        load_band_controls(OrcaExt::PartSizeLimits {});
        refresh_bands_feedback();
    });
    header_row->Add(defaults, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(header_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto *footprint_row = new wxBoxSizer(wxHORIZONTAL);
    make_label(footprint_row, _L("Footprint, % of the bed"));
    for (int i = 0; i < int(m_footprint_inputs.size()); ++i) {
        m_footprint_inputs[i] = make_input();
        footprint_row->Add(m_footprint_inputs[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    }
    sizer->Add(footprint_row, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    make_note(sizer, m_footprint_ranges);

    auto *height_row = new wxBoxSizer(wxHORIZONTAL);
    make_label(height_row, _L("Height, mm"));
    for (int i = 0; i < int(m_height_inputs.size()); ++i) {
        m_height_inputs[i] = make_input();
        height_row->Add(m_height_inputs[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    }
    sizer->Add(height_row, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    make_note(sizer, m_height_ranges);
    make_note(sizer, m_override_note);

    create_band_matrix(sizer);

    m_bands_readout = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_bands_readout->SetFont(::Label::Body_12);
    m_bands_readout->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    sizer->Add(m_bands_readout, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto *save = new Button(this, _L("Save bands"));
    save->SetStyle(ButtonStyle::Confirm, ButtonType::Compact);
    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { save_bands(); });
    sizer->Add(save, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
}

void PlateAutomationDialog::create_band_matrix(wxSizer *sizer)
{
    m_matrix_panel = new wxPanel(this);
    m_matrix_panel->SetBackgroundColour(*wxWHITE);

    auto *grid = new wxFlexGridSizer(1 + OrcaExt::kPartSizeHeightRows, 1 + OrcaExt::kPartSizeFootprintCols,
                                     FromDIP(2), FromDIP(8));
    auto make_cell = [this](const wxString &text, bool header) {
        auto *cell = new wxStaticText(m_matrix_panel, wxID_ANY, text);
        cell->SetFont(header ? ::Label::Body_12 : ::Label::Body_13);
        cell->SetForegroundColour(StateColor::darkModeColorFor(header ? wxColour("#5A6A6E") : wxColour("#323A3D")));
        return cell;
    };

    grid->Add(make_cell(wxEmptyString, true), 0, wxALIGN_CENTER);
    for (int column = 0; column < OrcaExt::kPartSizeFootprintCols; ++column) {
        m_matrix_col_labels[column] = make_cell(wxEmptyString, true);
        grid->Add(m_matrix_col_labels[column], 0, wxALIGN_CENTER);
    }
    for (int row = 0; row < OrcaExt::kPartSizeHeightRows; ++row) {
        m_matrix_row_labels[row] = make_cell(wxEmptyString, true);
        grid->Add(m_matrix_row_labels[row], 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
        for (int column = 0; column < OrcaExt::kPartSizeFootprintCols; ++column) {
            // The cells are the shared matrix, so this table cannot drift from the classifier.
            m_matrix_cells[row][column] = make_cell(plate_bucket_short_name(OrcaExt::kPartSizeMatrix[row][column]), false);
            grid->Add(m_matrix_cells[row][column], 0, wxALIGN_CENTER);
        }
    }

    m_matrix_panel->SetSizerAndFit(grid);
    m_matrix_panel->Hide();
    sizer->Add(m_matrix_panel, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
}

void PlateAutomationDialog::toggle_band_matrix()
{
    if (m_matrix_panel == nullptr)
        return;
    const bool show = ! m_matrix_panel->IsShown();
    m_matrix_panel->Show(show);
    if (m_matrix_toggle != nullptr)
        m_matrix_toggle->SetLabel(show ? _L("Hide table") : _L("Show table"));
    GetSizer()->SetSizeHints(this);
    Fit();
    Layout();
}

bool PlateAutomationDialog::read_band_controls(OrcaExt::PartSizeLimits &limits) const
{
    auto number = [](TextInput *input, double &out) {
        wxString text = input->GetTextCtrl()->GetValue();
        text.Trim(true).Trim(false);
        if (text.IsEmpty())
            return false;
        double value = 0.;
        if (! text.ToDouble(&value))
            return false;
        out = value;
        return true;
    };
    for (size_t i = 0; i < m_footprint_inputs.size(); ++i)
        if (! number(m_footprint_inputs[i], limits.fp_pct[i]))
            return false;
    for (size_t i = 0; i < m_height_inputs.size(); ++i)
        if (! number(m_height_inputs[i], limits.h_mm[i]))
            return false;
    return OrcaExt::valid_part_size_limits(limits);
}

void PlateAutomationDialog::load_band_controls(const OrcaExt::PartSizeLimits &limits)
{
    auto set = [](TextInput *input, double value) {
        input->GetTextCtrl()->ChangeValue(wxString::Format("%g", value));
    };
    for (size_t i = 0; i < m_footprint_inputs.size(); ++i)
        set(m_footprint_inputs[i], limits.fp_pct[i]);
    for (size_t i = 0; i < m_height_inputs.size(); ++i)
        set(m_height_inputs[i], limits.h_mm[i]);
}

void PlateAutomationDialog::refresh_bands_feedback()
{
    OrcaExt::PartSizeLimits limits;
    const bool              valid = read_band_controls(limits);

    auto set_text = [](wxStaticText *label, const wxString &text) {
        if (label != nullptr)
            label->SetLabel(text);
    };
    auto set_cell_style = [this](int row, int column, bool active) {
        wxStaticText *cell = m_matrix_cells[row][column];
        if (cell == nullptr)
            return;
        cell->SetForegroundColour(StateColor::darkModeColorFor(active ? wxColour("#00897B") : wxColour("#323A3D")));
        wxFont font = cell->GetFont();
        font.SetWeight(active ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        cell->SetFont(font);
    };

    if (! valid) {
        set_text(m_footprint_ranges, wxEmptyString);
        set_text(m_height_ranges, wxEmptyString);
        set_text(m_override_note, wxEmptyString);
        set_text(m_bands_readout, _L("Bands must increase: footprint 0.1-100 %, height 0.1-500 mm."));
        return;
    }

    set_text(m_footprint_ranges, footprint_ranges_line(limits));
    set_text(m_height_ranges, height_ranges_line(limits));
    set_text(m_override_note, wxString::Format(_L("A part over %s%% of the bed and above %s mm gets Large."),
                                               number_text(limits.fp_pct.back()), number_text(limits.h_mm.front())));
    for (int column = 0; column < OrcaExt::kPartSizeFootprintCols; ++column)
        set_text(m_matrix_col_labels[column], footprint_column_text(limits, column));
    for (int row = 0; row < OrcaExt::kPartSizeHeightRows; ++row)
        set_text(m_matrix_row_labels[row], height_row_text(limits, row));

    if (m_plate_footprints.empty()) {
        set_text(m_bands_readout, _L("This plate is empty."));
        for (int row = 0; row < OrcaExt::kPartSizeHeightRows; ++row)
            for (int column = 0; column < OrcaExt::kPartSizeFootprintCols; ++column)
                set_cell_style(row, column, false);
        return;
    }

    double     height = 0.;
    double     pct    = 0.;
    const auto bucket = OrcaExt::plate_part_bucket(m_plate_footprints, m_plate_area_mm2, limits, &height, &pct);
    set_text(m_bands_readout, wxString::Format(_L("This plate: %.1f mm tall, %.1f%% of the bed -> %s"),
                                               height, pct, plate_bucket_short_name(bucket)));

    const int hit_row    = OrcaExt::part_size_height_row(height, limits);
    const int hit_column = OrcaExt::part_size_footprint_column(pct, limits);
    for (int row = 0; row < OrcaExt::kPartSizeHeightRows; ++row)
        for (int column = 0; column < OrcaExt::kPartSizeFootprintCols; ++column)
            set_cell_style(row, column, row == hit_row && column == hit_column);

    m_bands_readout->GetParent()->Layout();
}

void PlateAutomationDialog::save_bands()
{
    OrcaExt::PartSizeLimits limits;
    if (! read_band_controls(limits)) {
        set_hint(_L("Bands must increase: footprint 0.1-100 %, height 0.1-500 mm."));
        return;
    }

    AppConfig &config = *wxGetApp().app_config;
    save_plate_bucket_limits(config, limits);
    config.save();

    set_hint(_L("Bands saved."));
    refresh_bands_feedback();
    if (m_on_applied)
        m_on_applied();
}

void PlateAutomationDialog::toggle_tile(size_t index)
{
    if (index >= m_tiles.size())
        return;
    Tile &tile = m_tiles[index];
    tile.selected = ! tile.selected;
    tile.button->SetValue(tile.selected);
    if (tile.selected)
        m_selected.push_back(index);
    else
        m_selected.erase(std::remove(m_selected.begin(), m_selected.end(), index), m_selected.end());
    m_focus = index;
    load_controls_from(tile.bed_type);
    set_hint(wxEmptyString);
}

void PlateAutomationDialog::load_controls_from(BedType bed_type)
{
    const AppConfig &config = *wxGetApp().app_config;
    const OrcaExt::PlateFlowDefaults    flow = load_plate_flow_defaults(config);
    const OrcaExt::PlateBedTempDefaults bed  = load_plate_bed_temp_defaults(config);

    refresh_flow_controls(OrcaExt::has_flow_default(flow, bed_type) ? OrcaExt::flow_default_for(flow, bed_type) : 0.);

    const auto it = bed.find(int(bed_type));
    for (int i = 0; i < OrcaExt::kPartSizeBucketCount; ++i)
        m_bucket_inputs[i]->SetValue(it == bed.end() ? 0 : it->second[i]);
}

void PlateAutomationDialog::refresh_flow_controls(double flow_percent)
{
    for (RadioBox *radio : m_flow_radios)
        radio->SetValue(false);

    // A value on the step grid selects that step; anything else (a custom number, or 0) lives in the
    // free-form field so it is visible instead of silently unselected.
    const auto it = std::find(kFlowChoices.begin(), kFlowChoices.end(), flow_percent);
    if (it != kFlowChoices.end() && flow_percent != 0.)
        m_flow_radios[size_t(it - kFlowChoices.begin())]->SetValue(true);
    else if (flow_percent == 0.)
        m_flow_radios.back()->SetValue(true);

    if (it == kFlowChoices.end())
        m_flow_custom->GetTextCtrl()->ChangeValue(wxString::Format("%d", int(flow_percent)));
    else
        m_flow_custom->GetTextCtrl()->ChangeValue(wxEmptyString);
}

void PlateAutomationDialog::refresh_tile_values()
{
    const OrcaExt::PlateFlowDefaults flow = load_plate_flow_defaults(*wxGetApp().app_config);
    for (Tile &tile : m_tiles) {
        if (! OrcaExt::has_flow_default(flow, tile.bed_type))
            tile.value->SetLabel("--");
        else
            tile.value->SetLabel(wxString::Format("%d%%", int(OrcaExt::flow_default_for(flow, tile.bed_type))));
    }
}

void PlateAutomationDialog::set_hint(const wxString &text)
{
    if (m_hint != nullptr)
        m_hint->SetLabel(text);
}

bool PlateAutomationDialog::read_flow_control(double &flow_percent) const
{
    wxString custom = m_flow_custom->GetTextCtrl()->GetValue();
    custom.Trim(true).Trim(false);
    if (! custom.IsEmpty()) {
        double value = 0.;
        if (! custom.ToDouble(&value))
            return false;
        if (value > OrcaExt::kFirstLayerFlowDeltaMax || value < OrcaExt::kFirstLayerFlowDeltaMin)
            return false;
        flow_percent = value;
        return true;
    }
    for (size_t i = 0; i < m_flow_radios.size(); ++i)
        if (m_flow_radios[i]->GetValue()) {
            flow_percent = kFlowChoices[i];
            return true;
        }
    flow_percent = 0.;
    return true;
}

void PlateAutomationDialog::apply()
{
    if (m_selected.empty()) {
        set_hint(_L("Select at least one plate first."));
        return;
    }

    double flow_percent = 0.;
    if (! read_flow_control(flow_percent)) {
        set_hint(wxString::Format(_L("Enter a percentage between %d and %d, or use a step or None."),
                                  int(OrcaExt::kFirstLayerFlowDeltaMin), int(OrcaExt::kFirstLayerFlowDeltaMax)));
        return;
    }

    std::array<int, OrcaExt::kPartSizeBucketCount> bed_offsets {};
    for (int i = 0; i < OrcaExt::kPartSizeBucketCount; ++i)
        bed_offsets[i] = OrcaExt::clamp_bed_temp_offset(m_bucket_inputs[i]->GetValue());

    AppConfig &config = *wxGetApp().app_config;
    OrcaExt::PlateFlowDefaults    flow = load_plate_flow_defaults(config);
    OrcaExt::PlateBedTempDefaults bed  = load_plate_bed_temp_defaults(config);
    for (size_t index : m_selected) {
        if (index >= m_tiles.size())
            continue;
        flow[int(m_tiles[index].bed_type)] = flow_percent;
        bed[int(m_tiles[index].bed_type)]  = bed_offsets;
    }
    save_plate_flow_defaults(config, flow);
    save_plate_bed_temp_defaults(config, bed);
    config.save();

    refresh_tile_values();
    set_hint(_L("Applied."));
    if (m_on_applied)
        m_on_applied();
}

void PlateAutomationDialog::on_dpi_changed(const wxRect & /*suggested_rect*/)
{
    GetSizer()->SetSizeHints(this);
    Fit();
    Layout();
}

} // namespace GUI
} // namespace Slic3r
