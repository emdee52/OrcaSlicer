// [ORCAPORT FILE] MaterialAutomationDialog - edit the local per-material table
// Source: original (design in porting/notes/MA-1.md)
//
// One row per material loaded on the plate, one column per value. An empty field means "leave the
// profile alone", which is what makes an empty table a no-op. The values are saved on this computer
// (AppConfig); the plate carries the *resolved* copy, so a project slices the same elsewhere.
#ifndef slic3r_GUI_MaterialAutomationDialog_hpp_
#define slic3r_GUI_MaterialAutomationDialog_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/dialog.h>
#include <wx/stattext.h>

#include "libslic3r/OrcaExt/MaterialAutomation.hpp"
#include "GUI_Utils.hpp"

class TextInput;

namespace Slic3r {
namespace GUI {

class MaterialAutomationDialog : public DPIDialog
{
public:
    // `materials` is the display order (the plate's filaments, deduplicated). `on_applied` runs after
    // every apply so the caller can refresh its rows and the plates without closing the dialog.
    MaterialAutomationDialog(wxWindow *parent, std::vector<std::string> materials,
                             const OrcaExt::MaterialTable &table, std::function<void()> on_applied);
    ~MaterialAutomationDialog() override = default;

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    struct Column
    {
        std::string material;
        TextInput  *bridge_density {nullptr};
        TextInput  *bridge_flow {nullptr};
        TextInput  *bridge_speed {nullptr};
        TextInput  *top_flow {nullptr};
        TextInput  *top_width {nullptr};
        TextInput  *top_speed {nullptr};
        TextInput  *outer_speed {nullptr};
        TextInput  *first_layer_flow {nullptr};
    };

    void add_row(wxSizer *grid, const wxString &label, TextInput *Column::*member);
    void set_hint(const wxString &text);
    bool read_number(const TextInput *input, double &value) const;
    bool read_line_width(const TextInput *input, double &value, bool &percent) const;
    void apply();

    std::vector<Column>  m_columns;
    wxStaticText        *m_hint {nullptr};
    std::function<void()> m_on_applied;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_MaterialAutomationDialog_hpp_
