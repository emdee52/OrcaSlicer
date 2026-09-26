// ORCA (FSM-1): parser for the filament_sync_profile_map app config value.

#include <catch2/catch_all.hpp>

#include "libslic3r/PresetBundle.hpp"

using Slic3r::parse_filament_preset_map;
using Map = std::vector<std::pair<std::string, std::string>>;

TEST_CASE("Filament preset map: empty and whitespace input yields nothing", "[FSM-1]")
{
    CHECK(parse_filament_preset_map("").empty());
    CHECK(parse_filament_preset_map("   \n\t  ").empty());
    CHECK(parse_filament_preset_map("\n;\n;;").empty());
}

TEST_CASE("Filament preset map: newline and semicolon separators", "[FSM-1]")
{
    CHECK(parse_filament_preset_map("A=B\nC=D") == Map{{"A", "B"}, {"C", "D"}});
    CHECK(parse_filament_preset_map("A=B;C=D") == Map{{"A", "B"}, {"C", "D"}});
    CHECK(parse_filament_preset_map("A=B\nC=D;E=F") == Map{{"A", "B"}, {"C", "D"}, {"E", "F"}});
}

TEST_CASE("Filament preset map: malformed lines are skipped", "[FSM-1]")
{
    CHECK(parse_filament_preset_map("no equals").empty());
    CHECK(parse_filament_preset_map("=B").empty());
    CHECK(parse_filament_preset_map("A=").empty());
    CHECK(parse_filament_preset_map("\n\nA=B\n\n").size() == 1);
}

TEST_CASE("Filament preset map: split on the first '=' only", "[FSM-1]")
{
    CHECK(parse_filament_preset_map("A=B=C") == Map{{"A", "B=C"}});
}

TEST_CASE("Filament preset map: values are trimmed", "[FSM-1]")
{
    CHECK(parse_filament_preset_map("  A  =  B  ") == Map{{"A", "B"}});
    CHECK(parse_filament_preset_map("Generic ABS=HF ABS") == Map{{"Generic ABS", "HF ABS"}});
}

TEST_CASE("Filament preset map: duplicate keys keep the first", "[FSM-1]")
{
    CHECK(parse_filament_preset_map("A=B\nA=C") == Map{{"A", "B"}});
}
