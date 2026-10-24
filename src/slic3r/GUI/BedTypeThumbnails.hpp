// [ORCAPORT FILE] BedTypeThumbnails - the plate picture for a bed type
// Source: original (design in porting/notes/UI-3.md)
//
// One place for the BedType -> resources/images/bed_*.png mapping, shared by the send dialog's plate
// thumbnail and the side panel's plate picker, so a new plate type cannot drift between them.
#pragma once

#include <string>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace GUI {

// Image resource name (no extension) for `bed_type`'s plate picture; empty when the type has none
// (btDefault/unset). create_scaled_bitmap throws on an empty name, so callers must check.
inline std::string bed_type_thumbnail_name(BedType bed_type)
{
    switch (bed_type) {
        case btPC:        return "bed_cool";
        case btEP:        return "bed_engineering";
        case btPEI:       return "bed_high_templ";
        case btPTE:       return "bed_pei";
        case btPCT:       return "bed_pei_cool";
        case btSuperTack: return "bed_cool_supertack";
        default:          return {};
    }
}

} // namespace GUI
} // namespace Slic3r
