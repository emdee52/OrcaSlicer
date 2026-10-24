#ifndef slic3r_PrintHostSendDialog_hpp_
#define slic3r_PrintHostSendDialog_hpp_

#include <map>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/colour.h>
#include <wx/string.h>
#include <wx/event.h>
#include <wx/dialog.h>

#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/Flashforge.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/OrcaExt/PlateAutomation.hpp" // [ORCAPORT:PA-1]
#include "libslic3r/ProjectTask.hpp"
class wxButton;
class wxTextCtrl;
class wxComboBox;
class ComboBox;
class wxDataViewListCtrl;
class wxFlexGridSizer;
class wxStaticText;
class wxWrapSizer;
class CheckBox;

namespace Slic3r { namespace GUI { class BitmapComboBox; class RadioBox; } }

namespace Slic3r {

namespace GUI {

class PrintHostSendDialog : public GUI::MsgDialog
{
public:
    PrintHostSendDialog(const boost::filesystem::path &path, PrintHostPostUploadActions post_actions, const wxArrayString& groups, const wxArrayString& storage_paths, const wxArrayString& storage_names, bool switch_to_device_tab);
    virtual ~PrintHostSendDialog() {}
    boost::filesystem::path filename() const;
    PrintHostPostUploadAction post_action() const;
    std::string group() const;
    std::string storage() const;
    bool switch_to_device_tab() const {return m_switch_to_device_tab;}
    // [ORCAPORT:BT-1] additive bed-temperature offset chosen in this dialog; 0 = unchanged
    int bed_temp_offset() const { return m_bed_temp_offset; }
    // [ORCAPORT:BF-1] first-layer bottom-surface flow reduction chosen in this dialog; 0 = stock
    double first_layer_flow_delta() const { return m_first_layer_flow_delta; }
    // [ORCAPORT:BF-1] true while the reduction still follows the plate automation (the user has not
    // chosen)
    bool first_layer_flow_auto() const { return m_first_layer_flow_auto; }
    // [ORCAPORT:BF-1] false for a send dialog that never built the row (Flashforge overrides init()),
    // so its caller must not write the two keys and reset a choice made from another dialog.
    bool has_first_layer_flow_row() const { return !m_first_layer_flow_radios.empty(); }
    // [ORCAPORT:PA-1] false for a send dialog that never built the bed-temperature row (Flashforge
    // overrides init()), so its caller must not write the offset for it.
    bool has_bed_temp_row() const { return !m_bed_temp_radios.empty(); }

    virtual void EndModal(int ret) override;
    virtual void init();
    virtual std::map<std::string, std::string> extendedInfo() const { return {}; }

protected:
    wxTextCtrl *txt_filename;
    wxComboBox *combo_groups;
    wxComboBox* combo_storage;
    PrintHostPostUploadAction post_upload_action;
    wxString    m_valid_suffix;
    wxString    m_preselected_storage;
    wxArrayString m_paths;
    bool m_switch_to_device_tab;

    boost::filesystem::path m_path;
    PrintHostPostUploadActions m_post_actions;
    wxArrayString m_storage_names;

    // [ORCAPORT:BT-1] BEGIN - bed-temperature offset row. The value is written to the project
    // config by the caller and applied at G-code export, so no preset is touched and no re-slice
    // is triggered.
    int                    m_bed_temp_offset {0};
    int                    m_bed_temp_base {0};
    BedType                m_bed_type {BedType::btDefault};
    wxString               m_bed_temp_plate_label;
    wxStaticText*          m_bed_temp_status {nullptr};
    std::vector<RadioBox*> m_bed_temp_radios;
    void                   set_bed_temp_offset(int offset);
    void                   update_bed_temp_status();
    void                   refresh_bed_temp_controls();
    // [ORCAPORT:BT-1] END

    // [ORCAPORT:BF-1] BEGIN - first-layer bottom-surface flow reduction row. Same plumbing as the
    // bed-temperature offset above: written to the project config by the caller, applied at G-code
    // export.
    double                 m_first_layer_flow_delta {0.};
    bool                   m_first_layer_flow_auto {true};
    double                 m_first_layer_flow_base {1.};
    wxStaticText*          m_first_layer_flow_status {nullptr};
    std::vector<RadioBox*> m_first_layer_flow_radios;
    void                   set_first_layer_flow_delta(double delta);
    void                   update_first_layer_flow_status();
    void                   refresh_first_layer_flow_controls();
    // [ORCAPORT:BF-1] END

    // [ORCAPORT:PA-1] local per-plate-type first-layer flow defaults, loaded from AppConfig when the
    // dialog opens and reloaded when the settings popup applies.
    OrcaExt::PlateFlowDefaults m_plate_flow_defaults;
    void                       open_plate_automation();
    void                       refresh_plate_automation();
    void                       reset_to_automation();
};


class PrintHostQueueDialog : public DPIDialog
{
public:
    class Event : public wxEvent
    {
    public:
        size_t job_id;
        int progress = 0;    // in percent
        wxString tag;
        wxString status;

        Event(wxEventType eventType, int winid, size_t job_id);
        Event(wxEventType eventType, int winid, size_t job_id, int progress);
        Event(wxEventType eventType, int winid, size_t job_id, wxString error);
        Event(wxEventType eventType, int winid, size_t job_id, wxString tag, wxString status);

        virtual wxEvent *Clone() const;
    };


    PrintHostQueueDialog(wxWindow *parent);

    void append_job(const PrintHostJob &job);
    void get_active_jobs(std::vector<std::pair<std::string, std::string>>& ret);

    virtual bool Show(bool show = true) override
    {
        if(!show)
            save_user_data(UDT_SIZE | UDT_POSITION | UDT_COLS);
        return DPIDialog::Show(show);
    }
protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_sys_color_changed() override;

private:
    enum Column {
        COL_ID,
        COL_PROGRESS,
        COL_STATUS,
        COL_HOST,
        COL_SIZE,
        COL_FILENAME,
        COL_ERRORMSG
    };

    enum JobState {
        ST_NEW,
        ST_PROGRESS,
        ST_ERROR,
        ST_CANCELLING,
        ST_CANCELLED,
        ST_COMPLETED,
    };

    enum { HEIGHT = 60, WIDTH = 30, SPACING = 5 };

    enum UserDataType{
        UDT_SIZE = 1,
        UDT_POSITION = 2,
        UDT_COLS = 4
    };

    wxButton *btn_cancel;
    wxButton *btn_error;
    wxDataViewListCtrl *job_list;
    // Note: EventGuard prevents delivery of progress evts to a freed PrintHostQueueDialog
    EventGuard on_progress_evt;
    EventGuard on_error_evt;
    EventGuard on_cancel_evt;
    EventGuard on_info_evt;

    JobState get_state(int idx);
    void set_state(int idx, JobState);
    void on_list_select();
    void on_progress(Event&);
    void on_error(Event&);
    void on_cancel(Event&);
    void on_info(Event&);
    // This vector keep adress and filename of uploads. It is used when checking for running uploads during exit.
    std::vector<std::pair<std::string, std::string>> upload_names;
    void save_user_data(int);
    bool load_user_data(int, std::vector<int>&);
};

class ElegooPrintHostSendDialog : public PrintHostSendDialog
{
public:
    ElegooPrintHostSendDialog(const boost::filesystem::path& path,
                              PrintHostPostUploadActions     post_actions,
                              const wxArrayString&           groups,
                              const wxArrayString&           storage_paths,
                              const wxArrayString&           storage_names,
                              bool                           switch_to_device_tab);

    virtual void EndModal(int ret) override;
    int          timeLapse() const { return m_timeLapse; }
    int          heatedBedLeveling() const { return m_heatedBedLeveling; }
    BedType      bedType() const { return m_BedType; }

    virtual void                               init() override;
    virtual std::map<std::string, std::string> extendedInfo() const override
    {
        return {{"bedType", std::to_string(static_cast<int>(m_BedType))},
                {"timeLapse", std::to_string(m_timeLapse)},
                {"heatedBedLeveling", std::to_string(m_heatedBedLeveling)}};
    }

private:
    BedType appBedType() const;
    void    refresh();

    const char* CONFIG_KEY_UPLOADANDPRINT    = "elegoolink_upload_and_print";
    const char* CONFIG_KEY_TIMELAPSE         = "elegoolink_timelapse";
    const char* CONFIG_KEY_HEATEDBEDLEVELING = "elegoolink_heated_bed_leveling";
    const char* CONFIG_KEY_BEDTYPE           = "elegoolink_bed_type";

private:
    wxStaticText* warning_text{nullptr};
    wxBoxSizer*   uploadandprint_sizer{nullptr};

    int     m_timeLapse;
    int     m_heatedBedLeveling;
    BedType m_BedType;
};

class CrealityPrintHostSendDialog : public PrintHostSendDialog
{
public:
    CrealityPrintHostSendDialog(const boost::filesystem::path& path,
                                PrintHostPostUploadActions     post_actions,
                                const wxArrayString&           groups,
                                const wxArrayString&           storage_paths,
                                const wxArrayString&           storage_names,
                                bool                           switch_to_device_tab,
                                PrintHost*                     printhost);

    virtual void                               init() override;
    virtual std::map<std::string, std::string> extendedInfo() const override;

private:
    static constexpr const char* CONFIG_KEY_ENABLESELFTEST = "crealityprint_enable_self_test";

    bool        m_enableSelfTest;
    PrintHost*  m_printhost;

    struct SlotInfo {
        std::string tool_id;   // e.g. "T1A"
        std::string type;      // e.g. "PLA"
        std::string color;     // e.g. "#ffffff"
        int         box_id;
        int         material_id;
    };
    std::vector<SlotInfo>   m_printer_slots;
    std::vector<BitmapComboBox*> m_slot_combos; // one per gcode filament
};

class FlashforgePrintHostSendDialog : public PrintHostSendDialog
{
public:
    FlashforgePrintHostSendDialog(const boost::filesystem::path&  path,
                                  PrintHostPostUploadActions      post_actions,
                                  const wxArrayString&            groups,
                                  const wxArrayString&            storage_paths,
                                  const wxArrayString&            storage_names,
                                  bool                            switch_to_device_tab,
                                  const Slic3r::Flashforge*       host,
                                  bool                            supports_material_station,
                                  std::vector<Slic3r::FlashforgeMaterialSlot> slots,
                                  const std::vector<FilamentInfo>& project_filaments);

    virtual void init() override;
    virtual void EndModal(int ret) override;
    virtual std::map<std::string, std::string> extendedInfo() const override;

private:
    struct MappingRow {
        int       tool_id {-1};
        wxWindow* card {nullptr};
    };

    void load_slots();
    bool ensure_slots_loaded(bool force_reload = false);
    void rebuild_mapping_rows();
    void auto_assign_mappings();
    void refresh_mapping_card(MappingRow& row);
    void sync_mapping_section_visibility();
    const Slic3r::FlashforgeMaterialSlot* find_slot_by_id(const std::string& slot_id_text) const;
    const FilamentInfo* find_filament_by_tool_id(int tool_id) const;
    bool slot_matches_filament(const Slic3r::FlashforgeMaterialSlot& slot, const FilamentInfo& filament) const;
    bool validate_before_close();
    std::string normalize_material(const std::string& material) const;
    wxColour to_wx_colour(const std::string& color) const;

private:
    const Slic3r::Flashforge*        m_host {nullptr};
    std::vector<FilamentInfo>        m_project_filaments;
    std::vector<Slic3r::FlashforgeMaterialSlot> m_slots;
    std::vector<MappingRow>          m_mapping_rows;
    wxBoxSizer*                      m_flashforge_options_sizer {nullptr};
    wxBoxSizer*                      m_mapping_section_sizer {nullptr};
    wxWrapSizer*                     m_mapping_wrap_sizer {nullptr};
    wxStaticText*                    m_status_text {nullptr};
    ::CheckBox*                      m_checkbox_leveling {nullptr};
    ::CheckBox*                      m_checkbox_timelapse {nullptr};
    ::CheckBox*                      m_checkbox_ifs {nullptr};
    bool                             m_leveling_before_print {true};
    bool                             m_time_lapse_video {false};
    bool                             m_use_material_station {false};
    bool                             m_supports_material_station {false};
    bool                             m_slots_loaded {false};

    const char* CONFIG_KEY_LEVELING  = "flashforge_leveling_before_print";
    const char* CONFIG_KEY_TIMELAPSE = "flashforge_timelapse_video";
    const char* CONFIG_KEY_IFS       = "flashforge_use_material_station";
};

wxDECLARE_EVENT(EVT_PRINTHOST_PROGRESS, PrintHostQueueDialog::Event);
wxDECLARE_EVENT(EVT_PRINTHOST_ERROR, PrintHostQueueDialog::Event);
wxDECLARE_EVENT(EVT_PRINTHOST_CANCEL, PrintHostQueueDialog::Event);
wxDECLARE_EVENT(EVT_PRINTHOST_INFO, PrintHostQueueDialog::Event);
}}

#endif
