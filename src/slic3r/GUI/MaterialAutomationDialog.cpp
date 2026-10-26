#include "MaterialAutomationDialog.hpp"

#include <wx/sizer.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MaterialAutomationStore.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/TextInput.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

namespace {

constexpr int kValueWidth = 110;

} // namespace

MaterialAutomationDialog::MaterialAutomationDialog(wxWindow *parent, std::vector<std::string> materials,
                                                   const OrcaExt::MaterialTable &table, std::function<void()> on_applied)
    : DPIDialog(parent, wxID_ANY, _L("Material automation"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_materials(std::move(materials))
    , m_table(table)
    , m_on_applied(std::move(on_applied))
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    auto *explain = new wxStaticText(this, wxID_ANY,
        _L("Saved on this computer only - no project, profile or downloaded 3mf changes it. The plate keeps a copy of the values it uses, so a project slices the same on another machine. An empty field leaves the profile's value alone."));
    explain->SetFont(::Label::Body_12);
    explain->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    explain->Wrap(FromDIP(520));
    main_sizer->Add(explain, 0, wxALL | wxEXPAND, FromDIP(12));

    if (m_materials.empty()) {
        auto *empty = new wxStaticText(this, wxID_ANY,
            _L("No materials in this project and none saved yet. Add a filament in the project, then reopen this dialog."));
        empty->SetFont(::Label::Body_13);
        empty->Wrap(FromDIP(520));
        main_sizer->Add(empty, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    } else {
        auto *material_row     = new wxBoxSizer(wxHORIZONTAL);
        auto *material_caption = new wxStaticText(this, wxID_ANY, _L("Material"));
        material_caption->SetFont(::Label::Body_13);
        material_caption->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
        material_row->Add(material_caption, 0, wxALL | wxALIGN_CENTER, FromDIP(2));

        m_material_choice = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(220), -1), 0, NULL, wxCB_READONLY);
        for (const std::string &material : m_materials)
            m_material_choice->Append(from_u8(material));
        m_material_choice->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
            const int selected = m_material_choice->GetSelection();
            if (selected < 0 || size_t(selected) == m_current)
                return;
            // Keep what was typed: a switch first stores this material, and only then loads the next.
            if (! store_current(true)) {
                m_material_choice->SetSelection(int(m_current));
                return;
            }
            select_material(size_t(selected));
        });
        material_row->Add(m_material_choice, 0, wxALL | wxALIGN_CENTER, FromDIP(2));
        main_sizer->Add(material_row, 0, wxLEFT | wxRIGHT, FromDIP(12));
    }

    auto *grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(6));
    grid->AddGrowableCol(1, 0);
    add_field(grid, _L("Bridge density (%)"), m_bridge_density);
    add_field(grid, _L("Bridge flow ratio"), m_bridge_flow);
    add_field(grid, _L("Bridge speed (mm/s)"), m_bridge_speed);
    add_field(grid, _L("Top surface flow ratio"), m_top_flow);
    add_field(grid, _L("Top surface line width (mm or %)"), m_top_width);
    add_field(grid, _L("Top surface speed (mm/s)"), m_top_speed);
    add_field(grid, _L("Outer wall speed (mm/s)"), m_outer_speed);
    add_field(grid, _L("First layer flow (%)"), m_first_layer_flow);
    main_sizer->Add(grid, 0, wxALL, FromDIP(12));

    m_hint = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_hint->SetFont(::Label::Body_12);
    m_hint->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    m_hint->Wrap(FromDIP(520));
    main_sizer->Add(m_hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    auto *buttons     = new wxBoxSizer(wxHORIZONTAL);
    auto *apply_button = new Button(this, _L("Apply"));
    apply_button->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    apply_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply(); });
    buttons->Add(apply_button, 0, wxALL, FromDIP(2));

    auto *close_button = new Button(this, _L("Close"));
    close_button->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CLOSE); });
    buttons->Add(close_button, 0, wxALL, FromDIP(2));
    main_sizer->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    SetSizerAndFit(main_sizer);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    if (! m_materials.empty()) {
        m_material_choice->SetSelection(0);
        select_material(0);
    }
}

void MaterialAutomationDialog::add_field(wxSizer *grid, const wxString &label, TextInput *&input)
{
    auto *caption = new wxStaticText(this, wxID_ANY, label);
    caption->SetFont(::Label::Body_13);
    caption->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    grid->Add(caption, 0, wxALIGN_CENTER_VERTICAL);

    input = new TextInput(this, wxEmptyString, "", "", wxDefaultPosition, wxSize(FromDIP(kValueWidth), -1), wxTE_PROCESS_ENTER);
    grid->Add(input, 0, wxALIGN_CENTER_VERTICAL);
}

void MaterialAutomationDialog::select_material(size_t index)
{
    if (index >= m_materials.size())
        return;
    m_current = index;
    m_hint->SetLabel(wxEmptyString);
    load_material(index);
    Layout();
}

void MaterialAutomationDialog::load_material(size_t index)
{
    const OrcaExt::MaterialValues *values = OrcaExt::find_material(m_table, m_materials[index]);

    auto set = [](TextInput *input, const wxString &text) { input->GetTextCtrl()->ChangeValue(text); };
    set(m_bridge_density, values != nullptr && values->has_bridge_density ? wxString::FromDouble(values->bridge_density, 2) : wxString());
    set(m_bridge_flow, values != nullptr && values->has_bridge_flow ? wxString::FromDouble(values->bridge_flow, 3) : wxString());
    set(m_bridge_speed, values != nullptr && values->has_bridge_speed ? wxString::FromDouble(values->bridge_speed) : wxString());
    set(m_top_flow, values != nullptr && values->has_top_flow ? wxString::FromDouble(values->top_flow, 3) : wxString());
    set(m_top_width, values != nullptr && values->has_top_width
                         ? wxString::FromDouble(values->top_width) + (values->top_width_percent ? "%" : "")
                         : wxString());
    set(m_top_speed, values != nullptr && values->has_top_speed ? wxString::FromDouble(values->top_speed) : wxString());
    set(m_outer_speed, values != nullptr && values->has_outer_speed ? wxString::FromDouble(values->outer_speed) : wxString());
    set(m_first_layer_flow,
        values != nullptr && values->has_first_layer_flow ? wxString::FromDouble(values->first_layer_flow, 2) : wxString());
}

bool MaterialAutomationDialog::store_current(bool quiet)
{
    if (m_materials.empty())
        return true;

    auto read = [](TextInput *input, double &value) {
        const wxString text = input->GetTextCtrl()->GetValue().Trim().Trim(false);
        if (text.empty())
            return false;
        double parsed = 0.;
        if (! text.ToCDouble(&parsed))
            return false;
        value = parsed;
        return true;
    };
    auto fail = [this, quiet](const wxString &message) {
        if (! quiet)
            set_hint(message);
        return false;
    };

    OrcaExt::MaterialValues values;
    double                   number = 0.;

    if (read(m_bridge_density, number)) {
        if (number < OrcaExt::kBridgeDensityMin || number > OrcaExt::kBridgeDensityMax)
            return fail(_L("Bridge density must be between 10 and 125 %."));
        values.has_bridge_density = true;
        values.bridge_density     = number;
    }
    if (read(m_bridge_flow, number)) {
        if (number < OrcaExt::kFlowRatioMin || number > OrcaExt::kFlowRatioMax)
            return fail(_L("Bridge flow ratio must be between 0 and 2."));
        values.has_bridge_flow = true;
        values.bridge_flow     = number;
    }
    if (read(m_bridge_speed, number)) {
        if (number < OrcaExt::kSpeedMin || number > OrcaExt::kSpeedMax)
            return fail(_L("Bridge speed must be between 1 and 1000 mm/s."));
        values.has_bridge_speed = true;
        values.bridge_speed     = number;
    }
    if (read(m_top_flow, number)) {
        if (number < OrcaExt::kFlowRatioMin || number > OrcaExt::kFlowRatioMax)
            return fail(_L("Top surface flow ratio must be between 0 and 2."));
        values.has_top_flow = true;
        values.top_flow     = number;
    }
    {
        wxString text = m_top_width->GetTextCtrl()->GetValue();
        text.Trim().Trim(false);
        if (! text.empty()) {
            const bool percent = text.EndsWith("%");
            if (percent)
                text.RemoveLast();
            double width = 0.;
            if (! text.ToCDouble(&width))
                return fail(_L("Top surface line width must be a number in mm, or a percentage of the nozzle diameter."));
            if (width <= 0. || width > (percent ? OrcaExt::kLineWidthPercentMax : OrcaExt::kLineWidthMmMax))
                return fail(_L("Top surface line width must be a positive value in mm or a percentage of the nozzle diameter."));
            values.has_top_width     = true;
            values.top_width         = width;
            values.top_width_percent = percent;
        }
    }
    if (read(m_top_speed, number)) {
        if (number < OrcaExt::kSpeedMin || number > OrcaExt::kSpeedMax)
            return fail(_L("Top surface speed must be between 1 and 1000 mm/s."));
        values.has_top_speed = true;
        values.top_speed     = number;
    }
    if (read(m_outer_speed, number)) {
        if (number < OrcaExt::kSpeedMin || number > OrcaExt::kSpeedMax)
            return fail(_L("Outer wall speed must be between 1 and 1000 mm/s."));
        values.has_outer_speed = true;
        values.outer_speed     = number;
    }
    if (read(m_first_layer_flow, number)) {
        if (number > OrcaExt::kFirstLayerFlowDeltaMax || number < OrcaExt::kFirstLayerFlowDeltaMin)
            return fail(_L("First layer flow must be a reduction between -50 and 0 %."));
        values.has_first_layer_flow = true;
        values.first_layer_flow     = number;
    }

    const std::string key = OrcaExt::normalize_material(m_materials[m_current]);
    if (values.empty())
        m_table.erase(key);
    else
        m_table[key] = values;

    save_material_table(*wxGetApp().app_config, m_table);
    if (m_on_applied)
        m_on_applied();
    return true;
}

void MaterialAutomationDialog::apply()
{
    if (! store_current(false))
        return;
    set_hint(_L("Saved."));
}

void MaterialAutomationDialog::set_hint(const wxString &text)
{
    if (m_hint != nullptr)
        m_hint->SetLabel(text);
}

void MaterialAutomationDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    this->SetSize(wxSize(FromDIP(620), -1));
    Layout();
    Fit();
}

} // namespace GUI
} // namespace Slic3r
