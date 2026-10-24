#include <catch2/catch_all.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/OrcaExt/PlateAutomation.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

// A 270 mm plate: the footprint bands are percentages of this.
constexpr double kBedArea = 270.0 * 270.0;

double footprint_pct(double pct) { return kBedArea * pct / 100.; }

template<int N> std::array<int, N> offsets(std::initializer_list<int> values)
{
    std::array<int, N> out {};
    size_t             i = 0;
    for (int value : values)
        if (i < out.size())
            out[i++] = value;
    return out;
}

} // namespace

// The classifier is a 6x5 matrix: six height rows (below the flat gate first) by five footprint
// columns. The biggest footprint column is Large from the flat gate up to the tall band, with no
// height row in between, so a print covering most of the bed is never classified Small.
TEST_CASE("Part size buckets follow the height and footprint matrix", "[OrcaPlateAutomation]") {
    using Bucket = OrcaExt::PartSizeBucket;
    const OrcaExt::PartSizeLimits limits;

    const std::array<double, OrcaExt::kPartSizeHeightRows> heights {0.5, 2., 10., 30., 70., 150.};
    const std::array<double, OrcaExt::kPartSizeFootprintCols> pcts {1., 10., 35., 60., 90.};
    const Bucket expected[OrcaExt::kPartSizeHeightRows][OrcaExt::kPartSizeFootprintCols] = {
        {Bucket::Small, Bucket::Small, Bucket::Small, Bucket::Small, Bucket::Small},
        {Bucket::Small, Bucket::Small, Bucket::SmallMedium, Bucket::SmallMedium, Bucket::Large},
        {Bucket::Small, Bucket::SmallMedium, Bucket::SmallMedium, Bucket::Medium, Bucket::Large},
        {Bucket::Medium, Bucket::Medium, Bucket::MediumLarge, Bucket::MediumLarge, Bucket::Large},
        {Bucket::MediumLarge, Bucket::Large, Bucket::Large, Bucket::Large, Bucket::Large},
        {Bucket::Tall, Bucket::Tall, Bucket::Tall, Bucket::Tall, Bucket::Tall},
    };

    for (int row = 0; row < OrcaExt::kPartSizeHeightRows; ++row)
        for (int column = 0; column < OrcaExt::kPartSizeFootprintCols; ++column) {
            CAPTURE(row, column, heights[row], pcts[column]);
            CHECK(OrcaExt::classify_part_size(footprint_pct(pcts[column]), kBedArea, heights[row], limits) == expected[row][column]);
        }

    // Boundaries belong to the higher band on both axes.
    CHECK(OrcaExt::classify_part_size(footprint_pct(90.), kBedArea, limits.h_mm[0], limits) == Bucket::Large);
    CHECK(OrcaExt::classify_part_size(footprint_pct(90.), kBedArea, limits.h_mm[0] - 0.01, limits) == Bucket::Small);
    CHECK(OrcaExt::classify_part_size(footprint_pct(24.99), kBedArea, 30., limits) == Bucket::Medium);
    CHECK(OrcaExt::classify_part_size(footprint_pct(25.), kBedArea, 30., limits) == Bucket::MediumLarge);
    CHECK(OrcaExt::classify_part_size(footprint_pct(74.99), kBedArea, 2., limits) == Bucket::SmallMedium);
    CHECK(OrcaExt::classify_part_size(footprint_pct(75.), kBedArea, 2., limits) == Bucket::Large);
    CHECK(OrcaExt::classify_part_size(footprint_pct(90.), kBedArea, limits.h_mm[1], limits) == Bucket::Large);
    CHECK(OrcaExt::classify_part_size(footprint_pct(90.), kBedArea, limits.h_mm.back(), limits) == Bucket::Tall);

    // A plate with no usable area, or a part with no footprint, falls back to the middle band.
    CHECK(OrcaExt::classify_part_size(footprint_pct(10.), 0., 10., limits) == Bucket::Medium);
    CHECK(OrcaExt::classify_part_size(0., kBedArea, 10., limits) == Bucket::Medium);
}

// The bed temperature serves the whole plate, so the riskiest part decides, and the deciding part's
// measurements are reported for the popup readout.
TEST_CASE("The plate's bucket is the riskiest part on it", "[OrcaPlateAutomation]") {
    using Bucket = OrcaExt::PartSizeBucket;
    using Part   = std::pair<double, double>; // footprint, height

    const std::vector<Part> thin_only {{kBedArea, 0.5}, {100., 0.2}};
    CHECK(OrcaExt::plate_part_bucket(thin_only, kBedArea) == Bucket::Small);

    const std::vector<Part> patch_and_medium {{kBedArea, 0.5}, {footprint_pct(10.), 30.}};
    CHECK(OrcaExt::plate_part_bucket(patch_and_medium, kBedArea) == Bucket::Medium);

    const std::vector<Part> patch_and_tower {{kBedArea, 0.5}, {50., 120.}};
    CHECK(OrcaExt::plate_part_bucket(patch_and_tower, kBedArea) == Bucket::Tall);

    double     height = 0.;
    double     pct    = 0.;
    const auto bucket = OrcaExt::plate_part_bucket({{kBedArea, 0.5}, {footprint_pct(60.), 30.}}, kBedArea,
                                                   OrcaExt::PartSizeLimits {}, &height, &pct);
    CHECK(bucket == Bucket::MediumLarge);
    CHECK_THAT(height, WithinAbs(30., 1e-9));
    CHECK_THAT(pct, WithinAbs(60., 1e-9));

    CHECK(OrcaExt::plate_part_bucket({}, kBedArea) == Bucket::Small);
}

// The bands are saved in AppConfig so they can be tuned without a rebuild; anything malformed, out of
// order, out of range or of a different length falls back to the defaults.
TEST_CASE("Size bands round-trip through the AppConfig string and fall back safely", "[OrcaPlateAutomation]") {
    const OrcaExt::PartSizeLimits defaults;
    CHECK(OrcaExt::serialize_part_size_limits(defaults) == "3,25,50,75;1,3,20,50,100");

    OrcaExt::PartSizeLimits custom;
    custom.fp_pct = {5., 10., 20., 30.};
    custom.h_mm   = {2., 4., 10., 20., 40.};
    CHECK(OrcaExt::serialize_part_size_limits(custom) == "5,10,20,30;2,4,10,20,40");

    const OrcaExt::PartSizeLimits parsed = OrcaExt::parse_part_size_limits("5,10,20,30;2,4,10,20,40");
    for (size_t i = 0; i < custom.fp_pct.size(); ++i)
        CHECK_THAT(parsed.fp_pct[i], WithinAbs(custom.fp_pct[i], 1e-9));
    for (size_t i = 0; i < custom.h_mm.size(); ++i)
        CHECK_THAT(parsed.h_mm[i], WithinAbs(custom.h_mm[i], 1e-9));

    // Non-increasing footprint, non-increasing heights, wrong counts (including the 2+4 form an older
    // build wrote), junk and out of range.
    for (const char *text : {"10,5,20,30;2,4,10,20,40", "5,10,20,30;10,4,10,20,40", "5,10,20;2,4,10,20,40",
                             "5,10,20,30;2,4,10,20", "3,15;3,20,50,100", "junk", "0.01,10,20,30;2,4,10,20,40",
                             "5,10,20,105;2,4,10,20,40", "5,10,20,30;2,4,10,20,600"}) {
        const OrcaExt::PartSizeLimits fallback = OrcaExt::parse_part_size_limits(text);
        for (size_t i = 0; i < defaults.fp_pct.size(); ++i)
            CHECK_THAT(fallback.fp_pct[i], WithinAbs(defaults.fp_pct[i], 1e-9));
    }
}

TEST_CASE("Classification follows custom size bands", "[OrcaPlateAutomation]") {
    OrcaExt::PartSizeLimits limits;
    limits.fp_pct = {5., 10., 20., 30.};
    limits.h_mm   = {2., 4., 10., 20., 40.};

    CHECK(OrcaExt::classify_part_size(kBedArea, kBedArea, 1.9, limits) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(footprint_pct(4.), kBedArea, 3., limits) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(footprint_pct(6.), kBedArea, 3., limits) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(footprint_pct(25.), kBedArea, 3., limits) == OrcaExt::PartSizeBucket::SmallMedium);
    CHECK(OrcaExt::classify_part_size(footprint_pct(35.), kBedArea, 3., limits) == OrcaExt::PartSizeBucket::Large);
    CHECK(OrcaExt::classify_part_size(footprint_pct(6.), kBedArea, 10., limits) == OrcaExt::PartSizeBucket::Medium);
    CHECK(OrcaExt::classify_part_size(footprint_pct(25.), kBedArea, 10., limits) == OrcaExt::PartSizeBucket::MediumLarge);
    CHECK(OrcaExt::classify_part_size(footprint_pct(35.), kBedArea, 10., limits) == OrcaExt::PartSizeBucket::Large);
    CHECK(OrcaExt::classify_part_size(footprint_pct(4.), kBedArea, 20., limits) == OrcaExt::PartSizeBucket::MediumLarge);
    CHECK(OrcaExt::classify_part_size(footprint_pct(6.), kBedArea, 20., limits) == OrcaExt::PartSizeBucket::Large);
    CHECK(OrcaExt::classify_part_size(footprint_pct(4.), kBedArea, 40., limits) == OrcaExt::PartSizeBucket::Tall);
}

// The footprint must come from the 2D hull: a part turned 45 degrees has up to twice the bounding
// box for the same material, which would move it up a band.
TEST_CASE("Instance footprint is the rotated hull, not the bounding box", "[OrcaPlateAutomation]") {
    Model        model;
    ModelObject *object = model.add_object("cube", "", make_cube(20., 20., 10.));
    ModelInstance *instance = object->add_instance();
    instance->set_rotation(Vec3d(0., 0., 45.));

    const auto [footprint, height] = OrcaExt::instance_footprint(*object, 0);
    CHECK_THAT(footprint, WithinAbs(400., 4.));  // 20 x 20, unchanged by the rotation
    CHECK_THAT(height, WithinAbs(10., 1e-6));

    const auto missing = OrcaExt::instance_footprint(*object, 7);
    CHECK_THAT(missing.first, WithinAbs(0., 1e-9));
    CHECK_THAT(missing.second, WithinAbs(0., 1e-9));
}

// AppConfig is a string; a hand-edited or stale value must never crash the dialog, and the encoding
// has to round-trip a configured zero ("print at the profile's flow").
TEST_CASE("Plate flow defaults round-trip through the AppConfig string", "[OrcaPlateAutomation]") {
    OrcaExt::PlateFlowDefaults defaults;
    defaults[int(BedType::btPC)]        = -20.;
    defaults[int(BedType::btPCT)]       = -12.;
    defaults[int(BedType::btSuperTack)] = 0.;

    const std::string          serialized = OrcaExt::serialize_flow_defaults(defaults);
    const OrcaExt::PlateFlowDefaults parsed = OrcaExt::parse_flow_defaults(serialized);
    CHECK(parsed == defaults);

    CHECK(OrcaExt::parse_flow_defaults("").empty());
    CHECK(OrcaExt::parse_flow_defaults("junk").empty());
    CHECK(OrcaExt::parse_flow_defaults("x:y;:;-5").empty());

    // Out-of-range values are clamped; an unknown plate type is dropped.
    const auto clamped = OrcaExt::parse_flow_defaults("1:-99;2:10;99:-20");
    REQUIRE(clamped.count(int(BedType::btPC)) == 1);
    CHECK_THAT(clamped.at(int(BedType::btPC)), WithinAbs(OrcaExt::kFirstLayerFlowDeltaMin, 1e-9));
    CHECK_THAT(clamped.at(int(BedType::btEP)), WithinAbs(0., 1e-9));
    CHECK(clamped.count(99) == 0);
}

TEST_CASE("Plate bed temperature defaults round-trip through the AppConfig string", "[OrcaPlateAutomation]") {
    using Buckets = OrcaExt::PlateBedTempDefaults;
    Buckets defaults;
    defaults[int(BedType::btPCT)]       = offsets<OrcaExt::kPartSizeBucketCount>({-15, -10, -5, 0, 0, 5});
    defaults[int(BedType::btSuperTack)] = offsets<OrcaExt::kPartSizeBucketCount>({-20, -20, -15, 0, 0, 0});

    const std::string serialized = OrcaExt::serialize_bed_temp_defaults(defaults);
    const Buckets     parsed     = OrcaExt::parse_bed_temp_defaults(serialized);
    CHECK(parsed == defaults);

    CHECK(OrcaExt::parse_bed_temp_defaults("").empty());
    CHECK(OrcaExt::parse_bed_temp_defaults("garbage").empty());

    // A short row is padded with zeros, a long one is cut, values are clamped.
    const Buckets short_row = OrcaExt::parse_bed_temp_defaults("1:-10");
    REQUIRE(short_row.count(int(BedType::btPC)) == 1);
    CHECK(short_row.at(int(BedType::btPC))[0] == -10);
    CHECK(short_row.at(int(BedType::btPC))[int(OrcaExt::PartSizeBucket::Tall)] == 0);

    const Buckets long_row = OrcaExt::parse_bed_temp_defaults("1:1,2,3,4,5,6,7,8");
    REQUIRE(long_row.count(int(BedType::btPC)) == 1);
    CHECK(long_row.at(int(BedType::btPC))[int(OrcaExt::PartSizeBucket::Tall)] == 6);

    const Buckets clamped = OrcaExt::parse_bed_temp_defaults("1:-99,99,0,0,0,0");
    CHECK(clamped.at(int(BedType::btPC))[0] == OrcaExt::kBedTempOffsetMin);
    CHECK(clamped.at(int(BedType::btPC))[1] == OrcaExt::kBedTempOffsetMax);
}

// The dialog asks "is this plate type configured?" before letting the automation own a row, and then
// looks the offset up by bucket.
TEST_CASE("Plate bed temperature defaults resolve per plate type and bucket", "[OrcaPlateAutomation]") {
    OrcaExt::PlateBedTempDefaults defaults;
    CHECK_FALSE(OrcaExt::has_bed_temp_default(defaults, BedType::btPCT));
    CHECK(OrcaExt::bed_temp_default_for(defaults, BedType::btPCT, OrcaExt::PartSizeBucket::Small) == 0);

    defaults[int(BedType::btPCT)] = offsets<OrcaExt::kPartSizeBucketCount>({-15, -10, -5, 0, 5, 10});
    CHECK(OrcaExt::has_bed_temp_default(defaults, BedType::btPCT));
    CHECK_FALSE(OrcaExt::has_bed_temp_default(defaults, BedType::btPC));

    CHECK(OrcaExt::bed_temp_default_for(defaults, BedType::btPCT, OrcaExt::PartSizeBucket::Small) == -15);
    CHECK(OrcaExt::bed_temp_default_for(defaults, BedType::btPCT, OrcaExt::PartSizeBucket::Medium) == -5);
    CHECK(OrcaExt::bed_temp_default_for(defaults, BedType::btPCT, OrcaExt::PartSizeBucket::Large) == 5);
    CHECK(OrcaExt::bed_temp_default_for(defaults, BedType::btPCT, OrcaExt::PartSizeBucket::Tall) == 10);
}
