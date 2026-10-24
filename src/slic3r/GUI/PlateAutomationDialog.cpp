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

// The size-band rules spelled out with the current numbers, for the section's tooltip.
wxString bands_summary(const OrcaExt::PartSizeLimits &limits)
{
    auto n = [](double value) { return wxString::Format("%g", value); };
    return wxString::Format(
        _L("Bands: under %s mm tall is Small at any footprint. %s-%s mm: Small below %s%% of the bed, otherwise Small-medium. %s-%s mm: Medium, Medium-large above %s%%. %s-%s mm: Medium-large, Large from %s%%. %s mm and up: Tall."),
        n(limits.h_thin_mm), n(limits.h_thin_mm), n(limits.h_mid_mm), n(limits.fp_small_pct),
        n(limits.h_mid_mm), n(limits.h_high_mm), n(limits.fp_mid_pct),
        n(limits.h_high_mm), n(limits.h_tall_mm), n(limits.fp_small_pct),
        n(limits.h_tall_mm));
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
    m_bands_caption = new wxStaticText(this, wxID_ANY, _L("Size bands (all plates)"));
    m_bands_caption->SetFont(::Label::Body_13);
    m_bands_caption->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    sizer->Add(m_bands_caption, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto make_label = [this](wxSizer *row, const wxString &text) {
        auto *item = new wxStaticText(this, wxID_ANY, text);
        item->SetFont(::Label::Body_12);
        item->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
        row->Add(item, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    };
    auto make_input = [this](const wxString &suffix) {
        auto *field = new TextInput(this, wxEmptyString, suffix, "", wxDefaultPosition, FromDIP(wxSize(80, 30)), wxTE_CENTRE);
        // Tuning loop: the readout follows what is typed, before anything is saved.
        field->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
            refresh_bands_feedback();
            event.Skip();
        });
        return field;
    };

    auto *footprint_row = new wxBoxSizer(wxHORIZONTAL);
    make_label(footprint_row, _L("Footprint"));
    m_footprint_inputs[0] = make_input("%");
    footprint_row->Add(m_footprint_inputs[0], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_footprint_inputs[1] = make_input("%");
    footprint_row->Add(m_footprint_inputs[1], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    make_label(footprint_row, _L("of the bed"));
    sizer->Add(footprint_row, 0, wxLEFT | wxRIGHT, FromDIP(12));

    auto *height_row = new wxBoxSizer(wxHORIZONTAL);
    make_label(height_row, _L("Height"));
    for (int i = 0; i < int(m_height_inputs.size()); ++i) {
        m_height_inputs[i] = make_input(wxEmptyString);
        height_row->Add(m_height_inputs[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    }
    make_label(height_row, _L("mm"));
    sizer->Add(height_row, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto *bands_row = new wxBoxSizer(wxHORIZONTAL);
    auto *save = new Button(this, _L("Save bands"));
    save->SetStyle(ButtonStyle::Confirm, ButtonType::Compact);
    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { save_bands(); });
    bands_row->Add(save, 0, wxALL | wxALIGN_CENTER, FromDIP(2));

    auto *defaults = new Button(this, _L("Defaults"));
    defaults->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    defaults->SetToolTip(_L("Fill in the built-in bands; press Save bands to keep them."));
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        load_band_controls(OrcaExt::PartSizeLimits {});
        refresh_bands_feedback();
    });
    bands_row->Add(defaults, 0, wxALL | wxALIGN_CENTER, FromDIP(2));
    sizer->Add(bands_row, 0, wxLEFT | wxRIGHT, FromDIP(12));

    m_bands_readout = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_bands_readout->SetFont(::Label::Body_12);
    m_bands_readout->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    sizer->Add(m_bands_readout, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
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
    return number(m_footprint_inputs[0], limits.fp_small_pct)
        && number(m_footprint_inputs[1], limits.fp_mid_pct)
        && number(m_height_inputs[0], limits.h_thin_mm)
        && number(m_height_inputs[1], limits.h_mid_mm)
        && number(m_height_inputs[2], limits.h_high_mm)
        && number(m_height_inputs[3], limits.h_tall_mm)
        && OrcaExt::valid_part_size_limits(limits);
}

void PlateAutomationDialog::load_band_controls(const OrcaExt::PartSizeLimits &limits)
{
    auto set = [](TextInput *input, double value) {
        input->GetTextCtrl()->ChangeValue(wxString::Format("%g", value));
    };
    set(m_footprint_inputs[0], limits.fp_small_pct);
    set(m_footprint_inputs[1], limits.fp_mid_pct);
    set(m_height_inputs[0], limits.h_thin_mm);
    set(m_height_inputs[1], limits.h_mid_mm);
    set(m_height_inputs[2], limits.h_high_mm);
    set(m_height_inputs[3], limits.h_tall_mm);
}

void PlateAutomationDialog::refresh_bands_feedback()
{
    OrcaExt::PartSizeLimits limits;
    const bool              valid = read_band_controls(limits);

    if (m_bands_caption != nullptr)
        m_bands_caption->SetToolTip(valid ? bands_summary(limits)
                                          : _L("Footprint bands must increase from 0.1 to 100 %; height bands must increase from 0.1 to 500 mm."));

    if (m_bands_readout == nullptr)
        return;

    if (! valid) {
        m_bands_readout->SetLabel(_L("Bands must increase: footprint 0.1-100 %, height 0.1-500 mm."));
        return;
    }
    if (m_plate_footprints.empty()) {
        m_bands_readout->SetLabel(_L("This plate is empty."));
        return;
    }

    double           height = 0.;
    double           pct    = 0.;
    const auto       bucket = OrcaExt::plate_part_bucket(m_plate_footprints, m_plate_area_mm2, limits, &height, &pct);
    m_bands_readout->SetLabel(wxString::Format(_L("This plate: %.1f mm tall, %.1f%% of the bed -> %s"),
                                               height, pct, plate_bucket_short_name(bucket)));
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
