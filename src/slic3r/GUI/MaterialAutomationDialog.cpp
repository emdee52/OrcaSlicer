#include "MaterialAutomationDialog.hpp"

#include <wx/sizer.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MaterialAutomationStore.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/TextInput.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

namespace {

constexpr int kValueWidth = 78;

} // namespace

MaterialAutomationDialog::MaterialAutomationDialog(wxWindow *parent, std::vector<std::string> materials,
                                                   const OrcaExt::MaterialTable &table, std::function<void()> on_applied)
    : DPIDialog(parent, wxID_ANY, _L("Material automation"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_on_applied(std::move(on_applied))
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    auto *explain = new wxStaticText(this, wxID_ANY,
        _L("Saved on this computer only - no project, profile or downloaded 3mf changes it. The plate keeps a copy of the values it uses, so a project slices the same on another machine. An empty field leaves the profile's value alone."));
    explain->SetFont(::Label::Body_12);
    explain->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    explain->Wrap(FromDIP(560));
    main_sizer->Add(explain, 0, wxALL | wxEXPAND, FromDIP(12));

    if (materials.empty()) {
        auto *empty = new wxStaticText(this, wxID_ANY, _L("No material rows: this plate has no filaments assigned yet."));
        empty->SetFont(::Label::Body_13);
        main_sizer->Add(empty, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    auto *grid = new wxFlexGridSizer(int(1 + materials.size()), FromDIP(6), FromDIP(6));
    grid->AddGrowableCol(0, 0);
    grid->Add(new wxStaticText(this, wxID_ANY, wxEmptyString), 0, wxALIGN_CENTER_VERTICAL);
    for (const std::string &material : materials) {
        auto *header = new wxStaticText(this, wxID_ANY, from_u8(material));
        header->SetFont(::Label::Body_13);
        grid->Add(header, 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
    }

    auto make_input = [this](const wxString &value) {
        auto *input = new TextInput(this, value, "", "", wxDefaultPosition, wxSize(FromDIP(kValueWidth), -1), wxTE_PROCESS_ENTER);
        return input;
    };

    m_columns.reserve(materials.size());
    for (const std::string &material : materials) {
        Column column;
        column.material = material;
        const OrcaExt::MaterialValues *values = OrcaExt::find_material(table, material);

        column.bridge_density = make_input(values != nullptr && values->has_bridge_density ? wxString::FromDouble(values->bridge_density, 2) : wxString());
        column.bridge_flow = make_input(values != nullptr && values->has_bridge_flow ? wxString::FromDouble(values->bridge_flow, 3) : wxString());
        column.bridge_speed = make_input(values != nullptr && values->has_bridge_speed ? wxString::FromDouble(values->bridge_speed) : wxString());
        column.top_flow = make_input(values != nullptr && values->has_top_flow ? wxString::FromDouble(values->top_flow, 3) : wxString());
        column.top_width = make_input(values != nullptr && values->has_top_width
            ? wxString::FromDouble(values->top_width) + (values->top_width_percent ? "%" : "")
            : wxString());
        column.top_speed = make_input(values != nullptr && values->has_top_speed ? wxString::FromDouble(values->top_speed) : wxString());
        column.outer_speed = make_input(values != nullptr && values->has_outer_speed ? wxString::FromDouble(values->outer_speed) : wxString());
        column.first_layer_flow = make_input(values != nullptr && values->has_first_layer_flow
            ? wxString::FromDouble(values->first_layer_flow, 2)
            : wxString());
        m_columns.push_back(column);
    }

    add_row(grid, _L("Bridge density (%)"), &Column::bridge_density);
    add_row(grid, _L("Bridge flow ratio"), &Column::bridge_flow);
    add_row(grid, _L("Bridge speed (mm/s)"), &Column::bridge_speed);
    add_row(grid, _L("Top surface flow ratio"), &Column::top_flow);
    add_row(grid, _L("Top surface line width (mm or %)"), &Column::top_width);
    add_row(grid, _L("Top surface speed (mm/s)"), &Column::top_speed);
    add_row(grid, _L("Outer wall speed (mm/s)"), &Column::outer_speed);
    add_row(grid, _L("First layer flow (%)"), &Column::first_layer_flow);

    main_sizer->Add(grid, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    m_hint = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_hint->SetFont(::Label::Body_12);
    m_hint->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    m_hint->Wrap(FromDIP(560));
    main_sizer->Add(m_hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    auto *buttons = new wxBoxSizer(wxHORIZONTAL);
    auto *apply_button = new Button(this, _L("Apply"));
    apply_button->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    apply_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply(); });
    buttons->Add(apply_button, 0, wxALL, FromDIP(2));

    auto *close = new Button(this, _L("Close"));

    close->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CLOSE); });
    buttons->Add(close, 0, wxALL, FromDIP(2));
    main_sizer->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    SetSizerAndFit(main_sizer);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void MaterialAutomationDialog::add_row(wxSizer *grid, const wxString &label, TextInput *Column::*member)
{
    auto *caption = new wxStaticText(this, wxID_ANY, label);
    caption->SetFont(::Label::Body_13);
    caption->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    grid->Add(caption, 0, wxALIGN_CENTER_VERTICAL);
    for (Column &column : m_columns)
        grid->Add(column.*member, 0, wxALIGN_CENTER_VERTICAL);
}

void MaterialAutomationDialog::set_hint(const wxString &text)
{
    if (m_hint != nullptr)
        m_hint->SetLabel(text);
}

bool MaterialAutomationDialog::read_number(const TextInput *input, double &value) const
{
    const wxString text = input->GetTextCtrl()->GetValue().Trim().Trim(false);
    if (text.empty())
        return false;
    double parsed = 0.;
    if (! text.ToCDouble(&parsed))
        return false;
    value = parsed;
    return true;
}

bool MaterialAutomationDialog::read_line_width(const TextInput *input, double &value, bool &percent) const
{
    wxString text = input->GetTextCtrl()->GetValue();
    text.Trim().Trim(false);
    if (text.empty())
        return false;
    percent = text.EndsWith("%");
    if (percent)
        text.RemoveLast();
    double parsed = 0.;
    if (! text.ToCDouble(&parsed))
        return false;
    value   = parsed;
    return true;
}

void MaterialAutomationDialog::apply()
{
    OrcaExt::MaterialTable table = load_material_table(*wxGetApp().app_config);

    for (Column &column : m_columns) {
        OrcaExt::MaterialValues values;
        double                   number = 0.;

        if (read_number(column.bridge_density, number)) {
            if (number < OrcaExt::kBridgeDensityMin || number > OrcaExt::kBridgeDensityMax) {
                set_hint(_L("Bridge density must be between 10 and 125 %."));
                return;
            }
            values.has_bridge_density = true;
            values.bridge_density     = number;
        }
        if (read_number(column.bridge_flow, number)) {
            if (number < OrcaExt::kFlowRatioMin || number > OrcaExt::kFlowRatioMax) {
                set_hint(_L("Bridge flow ratio must be between 0 and 2."));
                return;
            }
            values.has_bridge_flow = true;
            values.bridge_flow     = number;
        }
        if (read_number(column.bridge_speed, number)) {
            if (number < OrcaExt::kSpeedMin || number > OrcaExt::kSpeedMax) {
                set_hint(_L("Bridge speed must be between 1 and 1000 mm/s."));
                return;
            }
            values.has_bridge_speed = true;
            values.bridge_speed     = number;
        }
        if (read_number(column.top_flow, number)) {
            if (number < OrcaExt::kFlowRatioMin || number > OrcaExt::kFlowRatioMax) {
                set_hint(_L("Top surface flow ratio must be between 0 and 2."));
                return;
            }
            values.has_top_flow = true;
            values.top_flow     = number;
        }
        {
            double width = 0.;
            bool   percent = false;
            if (read_line_width(column.top_width, width, percent)) {
                if (width <= 0. || width > (percent ? OrcaExt::kLineWidthPercentMax : OrcaExt::kLineWidthMmMax)) {
                    set_hint(_L("Top surface line width must be a positive value in mm or a percentage of the nozzle diameter."));
                    return;
                }
                values.has_top_width     = true;
                values.top_width         = width;
                values.top_width_percent = percent;
            }
        }
        if (read_number(column.top_speed, number)) {
            if (number < OrcaExt::kSpeedMin || number > OrcaExt::kSpeedMax) {
                set_hint(_L("Top surface speed must be between 1 and 1000 mm/s."));
                return;
            }
            values.has_top_speed = true;
            values.top_speed     = number;
        }
        if (read_number(column.outer_speed, number)) {
            if (number < OrcaExt::kSpeedMin || number > OrcaExt::kSpeedMax) {
                set_hint(_L("Outer wall speed must be between 1 and 1000 mm/s."));
                return;
            }
            values.has_outer_speed = true;
            values.outer_speed     = number;
        }
        if (read_number(column.first_layer_flow, number)) {
            if (number > OrcaExt::kFirstLayerFlowDeltaMax || number < OrcaExt::kFirstLayerFlowDeltaMin) {
                set_hint(_L("First layer flow must be a reduction between -50 and 0 %."));
                return;
            }
            values.has_first_layer_flow = true;
            values.first_layer_flow     = number;
        }

        const std::string key = OrcaExt::normalize_material(column.material);
        if (values.empty())
            table.erase(key);
        else
            table[key] = values;
    }

    save_material_table(*wxGetApp().app_config, table);
    set_hint(_L("Saved."));
    if (m_on_applied)
        m_on_applied();
}

void MaterialAutomationDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    this->SetSize(wxSize(FromDIP(640), -1));
    Layout();
    Fit();
}

} // namespace GUI
} // namespace Slic3r
