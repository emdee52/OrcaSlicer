// [ORCAPORT FILE] MaterialAutomationDialog - edit the local per-material table
// Source: original (design in porting/notes/MA-1.md)
//
// One material at a time, chosen from the list the project carries: a plate's own filament list is
// empty until it is sliced, so the selector is the project's filament types plus any material that
// already has a row. An empty field means "leave the profile alone", which is what makes an empty
// table a no-op. The values are saved on this computer (AppConfig); the plate carries the *resolved*
// copy, so a project slices the same elsewhere.
#ifndef slic3r_GUI_MaterialAutomationDialog_hpp_
#define slic3r_GUI_MaterialAutomationDialog_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/dialog.h>
#include <wx/stattext.h>

#include "libslic3r/OrcaExt/MaterialAutomation.hpp"
#include "GUI_Utils.hpp"

class ComboBox;
class TextInput;

namespace Slic3r {
namespace GUI {

class MaterialAutomationDialog : public DPIDialog
{
public:
    // `materials` is the selector's list, already ordered and deduplicated. `on_applied` runs after
    // every save so the caller can refresh its rows without closing the dialog.
    MaterialAutomationDialog(wxWindow *parent, std::vector<std::string> materials,
                             const OrcaExt::MaterialTable &table, std::function<void()> on_applied);
    ~MaterialAutomationDialog() override = default;

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void add_field(wxSizer *grid, const wxString &label, TextInput *&input);
    void load_material(size_t index);
    void select_material(size_t index);
    bool store_current(bool quiet);
    void apply();
    void set_hint(const wxString &text);

    TextInput *m_bridge_density {nullptr};
    TextInput *m_bridge_flow {nullptr};
    TextInput *m_bridge_speed {nullptr};
    TextInput *m_top_flow {nullptr};
    TextInput *m_top_width {nullptr};
    TextInput *m_top_speed {nullptr};
    TextInput *m_outer_speed {nullptr};
    TextInput *m_first_layer_flow {nullptr};

    ComboBox             *m_material_choice {nullptr};
    wxStaticText         *m_hint {nullptr};
    std::vector<std::string> m_materials;
    OrcaExt::MaterialTable   m_table; // working copy, saved on Apply
    size_t                   m_current {0};
    std::function<void()>    m_on_applied;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_MaterialAutomationDialog_hpp_
