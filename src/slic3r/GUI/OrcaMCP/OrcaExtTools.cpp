// [ORCAPORT FILE] MCP-2 - OraExt introspection tools for the embedded MCP server.
// Exposes this fork's opt-in feature state (app preferences, Support Zones / paint
// annotations, the OraExt config keys) and the app's own log, which the upstream
// OrcaMCP tools do not know about.
#include "OrcaMCPServer.hpp"
#include "OrcaMCPCommon.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/OrcaExt/FreeZ.hpp"
#include "libslic3r/OrcaExt/IdleToolPowerDown.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <ctime>
#include <fstream>

namespace Slic3r { namespace GUI {

namespace {

using OrcaMCP::run_on_main_thread;

// App-level OraExt preferences (AppConfig keys). These are not in PrintConfig, so the
// generic config tools cannot see them.
struct AppPrefInfo {
    const char* key;
    bool        default_value;
    const char* description;
};

const std::vector<AppPrefInfo>& orcaext_app_pref_infos()
{
    static const std::vector<AppPrefInfo> infos = {
        {"orca_ext_idle_tool_power_down", false, "Turn off unused hotends fully (0 C) after their last extrusion"},
        {"orca_ext_idle_tool_deep_sleep", true,  "Extra Energy Save: also switch a tool off while it waits"},
        {"orca_ext_free_z",               false, "Allow free Z placement (objects stay off the bed)"},
        {"orca_ext_snap_drag",            false, "Snap & Drag"},
        {"orca_ext_snap_drag_bed",        true,  "Snap & Drag: also snap to the bed"},
        {"orca_ext_snap_drag_group",      false, "Snap & Drag: snap to other objects"},
        {"orca_ext_mcp",                  false, "Embedded MCP server (localhost:13618/mcp)"},
    };
    return infos;
}

bool is_known_app_pref(const std::string& key)
{
    for (const auto& info : orcaext_app_pref_infos())
        if (key == info.key)
            return true;
    return false;
}

nlohmann::json get_app_preferences_json()
{
    nlohmann::json prefs = nlohmann::json::array();
    for (const auto& info : orcaext_app_pref_infos()) {
        prefs.push_back({
            {"key",         info.key},
            {"value",       wxGetApp().app_config->get_bool(info.key)},
            {"default",     info.default_value},
            {"description", info.description}
        });
    }
    return {{"preferences", prefs}};
}

void apply_app_preference(const std::string& key, bool value)
{
    if (key == "orca_ext_idle_tool_power_down")
        OrcaExt::set_idle_tool_power_down(value);
    else if (key == "orca_ext_idle_tool_deep_sleep")
        OrcaExt::set_idle_tool_power_down_deep(value);
    else if (key == "orca_ext_free_z")
        OrcaExt::set_free_z(value);
    else if (key == "orca_ext_mcp") {
        if (value)
            wxGetApp().start_http_server();
        else
            wxGetApp().stop_http_server();
    }
    // GravitySnap reads orca_ext_snap_drag* straight from AppConfig; no setter needed.
}

// Print/region/object config keys added by the OraExt features. Kept explicit so the list
// is stable and self-documenting (get_valid_config_keys already lists everything).
const std::vector<std::string>& orcaext_config_keys()
{
    static const std::vector<std::string> keys = {
        // PQ-1
        "bridge_expansion_extra",
        // PQ-2
        "inner_wall_overhang_slowdown", "inner_wall_overhang_speed_pct", "inner_wall_overhang_reach_pct",
        // PQ-3 (NeoArachne hybrid)
        "hybrid_outer_wall", "hybrid_inner_walls", "hybrid_gap_fill", "hybrid_allowed_overlap_pct",
        "hybrid_min_bead_width_pct", "hybrid_max_bead_width_pct", "hybrid_min_feature_size_pct",
        "hybrid_keep_short_tails", "hybrid_pin_outer_width", "hybrid_bead_count_hysteresis_pct",
        "hybrid_transition_filter_dist_mm",
        // SU-1 / SU-4 / SU-5 / SU-4b
        "support_cross_object_avoidance",
        "wavesupport_roof_pattern", "wavesupport_roof_order", "wavesupport_roof_reverse", "wavesupport_wall_loops",
        "support_zone_gesture", "support_zone_lean_deg", "support_zone_roof_only", "support_zone_solid",
        "support_zone_land_only",
        "support_neoweave_enabled", "support_neoweave_target", "support_neoweave_amplitude",
        "support_neoweave_period", "support_neoweave_max_z_speed",
        // SU-6
        "support_interface_base_layers",
        // SU-9
        "support_interface_weave_enable", "support_interface_weave_layers", "support_interface_weave_pitch", "support_interface_weave_flush",
        // SU-10
        "support_interface_contact_speed", "support_interface_contact_line_width",
        // SU-11
        "support_interface_serpentine", "support_interface_base_bridge", "support_interface_perimeter",
        // SU-8
        "transition_interface_base_enable", "transition_interface_base_layers", "transition_interface_base_speed", "transition_interface_base_flow", "transition_interface_base_fan", "transition_interface_base_temp_delta",
        "transition_object_interface_enable", "transition_object_interface_layers", "transition_object_interface_speed", "transition_object_interface_flow", "transition_object_interface_fan", "transition_object_interface_temp_delta",
        "transition_interface_object_enable", "transition_interface_object_layers", "transition_interface_object_speed", "transition_interface_object_flow", "transition_interface_object_fan", "transition_interface_object_temp_delta",
        // MT-4
        "mmu_segmented_region_extra_walls",
        // PF-1 / PF-2 / PF-9
        "seam_notch_width", "seam_notch_angle", "seam_notch_target",
        "counterbore_bridge_layers",
        "interlock_perimeters_enabled", "interlock_perimeter_count", "interlock_regular_perimeters",
        "interlock_solid_layers_top", "interlock_solid_layers_bottom", "interlock_perimeter_strength",
        "interlock_perimeter_overlap"
    };
    return keys;
}

nlohmann::json list_orcaext_keys_json()
{
    const DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config();
    nlohmann::json out = nlohmann::json::array();
    for (const std::string& key : orcaext_config_keys()) {
        const ConfigOption* opt = cfg.option(key);
        nlohmann::json entry = {
            {"key",     key},
            {"present", opt != nullptr}
        };
        if (opt != nullptr)
            entry["value"] = opt->serialize();
        const ConfigOptionDef* def = print_config_def.get(key);
        if (def != nullptr && def->default_value.get() != nullptr)
            entry["default"] = def->default_value->serialize();
        out.push_back(entry);
    }
    return {{"keys", out}, {"count", out.size()}};
}

nlohmann::json read_volume_orcaext_features(const ModelVolume* volume)
{
    nlohmann::json painted = {
        {"support",            volume->is_fdm_support_painted()},
        {"seam",               volume->is_seam_painted()},
        {"multi_material",     volume->is_mm_painted()},
        {"fuzzy_skin",         volume->is_fuzzy_skin_painted()},
        {"counterbore_bridge", volume->is_counterbore_bridge_painted()}
    };

    nlohmann::json zones = nlohmann::json::object();
    if (const ConfigOptionString* gesture = dynamic_cast<const ConfigOptionString*>(volume->config.option("support_zone_gesture")))
        zones["gesture"] = gesture->value;
    if (const ConfigOptionFloat* lean = dynamic_cast<const ConfigOptionFloat*>(volume->config.option("support_zone_lean_deg")))
        zones["lean_deg"] = lean->value;
    if (const ConfigOptionBool* roof_only = dynamic_cast<const ConfigOptionBool*>(volume->config.option("support_zone_roof_only")))
        zones["roof_only"] = roof_only->value;
    if (const ConfigOptionBool* solid = dynamic_cast<const ConfigOptionBool*>(volume->config.option("support_zone_solid")))
        zones["solid"] = solid->value;
    if (const ConfigOptionBool* land_only = dynamic_cast<const ConfigOptionBool*>(volume->config.option("support_zone_land_only")))
        zones["land_only"] = land_only->value;

    return {{"painted", painted}, {"support_zones", zones},
            {"any_painted", volume->is_any_painted()}};
}

nlohmann::json get_object_features_json(const nlohmann::json& params)
{
    const bool    filter    = params.contains("object_id") && !params["object_id"].is_null();
    const int     object_id = filter ? params["object_id"].get<int>() : -1;
    const Model&  model     = wxGetApp().plater()->model();

    nlohmann::json objects = nlohmann::json::array();
    for (const ModelObject* obj : model.objects) {
        if (filter && obj->id().id != object_id)
            continue;
        nlohmann::json o = {
            {"object_id", obj->id().id},
            {"name",      obj->name},
            {"volumes",   nlohmann::json::array()}
        };
        for (const ModelVolume* volume : obj->volumes) {
            o["volumes"].push_back({
                {"volume_id", volume->id().id},
                {"name",      volume->name},
                {"type",      int(volume->type())},
                {"features",  read_volume_orcaext_features(volume)}
            });
        }
        objects.push_back(o);
    }
    return {{"objects", objects}, {"count", objects.size()}};
}

boost::filesystem::path newest_log_file()
{
    const boost::filesystem::path dir = boost::filesystem::path(Slic3r::data_dir()) / "log";
    if (!boost::filesystem::exists(dir))
        return {};

    boost::filesystem::path newest;
    std::time_t newest_time{};
    for (boost::filesystem::directory_iterator it(dir), end; it != end; ++it) {
        if (!boost::filesystem::is_regular_file(it->path()))
            continue;
        const std::string name = it->path().filename().string();
        if (name.rfind("debug_", 0) != 0)
            continue;
        const std::time_t t = boost::filesystem::last_write_time(it->path());
        if (newest.empty() || t > newest_time) {
            newest      = it->path();
            newest_time = t;
        }
    }
    return newest;
}

nlohmann::json get_log_tail_json(const nlohmann::json& params)
{
    int lines = params.value("lines", 200);
    lines = std::max(1, std::min(lines, 5000));

    const boost::filesystem::path file = newest_log_file();
    if (file.empty())
        return {{"status", "no_log_file"}};

    std::ifstream in(file.string());
    if (!in)
        return {{"status", "open_failed"}, {"file", file.string()}};

    std::vector<std::string> all;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() > 4000)
            line.resize(4000);
        all.push_back(std::move(line));
    }

    const size_t wanted = size_t(std::min<size_t>(lines, all.size()));
    std::vector<std::string> tail(all.end() - wanted, all.end());
    return {{"status", "ok"}, {"file", file.string()}, {"lines", tail}};
}

} // namespace

void OrcaMCPServer::register_orcaext_tools()
{
    // ==================== ORAEXT APP PREFERENCES ====================

    register_tool({
        "get_app_preferences",
        "Get this fork's OraExt app-level preferences (idle tool power down, free-Z placement, "
        "Snap & Drag, MCP server) with current and default values. These are AppConfig keys, so "
        "the generic config tools cannot see them.",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            return run_on_main_thread([]() -> nlohmann::json {
                return get_app_preferences_json();
            });
        }
    });

    register_tool({
        "set_app_preference",
        "Set one OraExt app-level preference. Applies the same side effects as the Preferences "
        "checkboxes (OrcaExt setters / MCP server start-stop).",
        {
            {"type", "object"},
            {"properties", {
                {"key", {
                    {"type", "string"},
                    {"description", "Preference key, e.g. orca_ext_free_z; call get_app_preferences for the list."}
                }},
                {"value", {{"type", "boolean"}, {"description", "New value"}}}
            }},
            {"required", {"key", "value"}}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            if (!params.contains("key") || !params.contains("value"))
                return {{"status", "error"}, {"error", "Both 'key' and 'value' are required"}};
            const std::string key = params["key"].get<std::string>();
            if (!is_known_app_pref(key))
                return {{"status", "error"}, {"error", "Unknown OraExt preference: " + key}};
            const bool value = params["value"].get<bool>();

            return run_on_main_thread([key, value]() -> nlohmann::json {
                wxGetApp().app_config->set_bool(key, value);
                wxGetApp().app_config->save();
                apply_app_preference(key, value);
                nlohmann::json result = get_app_preferences_json();
                result["status"]  = "success";
                result["changed"] = {{"key", key}, {"value", value}};
                return result;
            });
        }
    });

    // ==================== ORAEXT CONFIG KEYS ====================

    register_tool({
        "list_orcaext_keys",
        "List this fork's OraExt print/region/object config keys with their current (active "
        "preset) value and default. Use these with apply_config / set_object_config.",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            return run_on_main_thread([]() -> nlohmann::json {
                return list_orcaext_keys_json();
            });
        }
    });

    // ==================== PER-VOLUME ORAEXT FEATURES ====================

    register_tool({
        "get_object_features",
        "Get this fork's per-volume annotations and feature state: painted facets (support, seam, "
        "multi-material, fuzzy skin, counterbore bridge) and Support Zones settings. Fills the gap "
        "left by get_scene_info's empty with_model_object_features in this fork.",
        {
            {"type", "object"},
            {"properties", {
                {"object_id", {
                    {"type", "integer"},
                    {"description", "Optional object index; omit for all objects."}
                }}
            }}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            return run_on_main_thread([params]() -> nlohmann::json {
                return get_object_features_json(params);
            });
        }
    });

    // ==================== DIAGNOSTICS ====================

    register_tool({
        "get_log_tail",
        "Return the tail of the newest OrcaSlicer debug log (%APPDATA%/OrcaSlicer/log). Useful for "
        "diagnosing slicing errors and crashes without a screenshot.",
        {
            {"type", "object"},
            {"properties", {
                {"lines", {
                    {"type", "integer"},
                    {"description", "Number of trailing lines to return (default 200, max 5000)."}
                }}
            }}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            return get_log_tail_json(params);
        }
    });

    register_tool({
        "get_suppressed_dialogs",
        "Return the info/dialog messages that were captured (not shown) while MCP was driving the "
        "app. Complements active_warnings.",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [](const nlohmann::json& params) -> nlohmann::json {
            return run_on_main_thread([]() -> nlohmann::json {
                const std::vector<std::string> messages = get_mcp_suppressed_messages();
                return nlohmann::json{{"count", messages.size()}, {"messages", messages}};
            });
        }
    });

    BOOST_LOG_TRIVIAL(info) << "OrcaMCPServer: Registered OraExt tools";
}

}} // namespace Slic3r::GUI
