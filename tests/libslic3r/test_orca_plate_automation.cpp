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

// A 270 mm plate: the footprint cuts are percentages of this.
constexpr double kBedArea = 270.0 * 270.0;

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

// Warping is a height problem first and an area problem second: a part under 3 mm tall has no lever
// arm and no gradient across its thickness, so it never needs bed heat, whatever it covers. Above
// that, the matrix in classify_part_size decides. Boundaries are inclusive at the top of each band.
TEST_CASE("Part size buckets follow the height and footprint matrix", "[OrcaPlateAutomation]") {
    const double small_cut = kBedArea * OrcaExt::kPartSizeAreaPctCuts[0] / 100.;
    const double mid_cut   = kBedArea * OrcaExt::kPartSizeAreaPctCuts[1] / 100.;

    // Thin gate: a 1-layer test or a full-bed patch under 3 mm is always Small.
    CHECK(OrcaExt::classify_part_size(100., kBedArea, 0.2) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(kBedArea, kBedArea, 1.) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(kBedArea, kBedArea, OrcaExt::kThinPartHeightMm - 0.01) == OrcaExt::PartSizeBucket::Small);

    // 3-20 mm: only a sliver of the bed stays Small.
    CHECK(OrcaExt::classify_part_size(small_cut - 1., kBedArea, OrcaExt::kThinPartHeightMm) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(small_cut, kBedArea, 10.) == OrcaExt::PartSizeBucket::SmallMedium);
    CHECK(OrcaExt::classify_part_size(kBedArea, kBedArea, OrcaExt::kMidPartHeightMm - 0.01) == OrcaExt::PartSizeBucket::SmallMedium);

    // 20-50 mm: Medium up to 15 % of the bed, Medium-large above it.
    CHECK(OrcaExt::classify_part_size(small_cut + 1., kBedArea, OrcaExt::kMidPartHeightMm) == OrcaExt::PartSizeBucket::Medium);
    CHECK(OrcaExt::classify_part_size(mid_cut, kBedArea, 30.) == OrcaExt::PartSizeBucket::Medium);
    CHECK(OrcaExt::classify_part_size(mid_cut + 1., kBedArea, 30.) == OrcaExt::PartSizeBucket::MediumLarge);

    // 50-100 mm: a slender footprint is Medium-large (it lifts sooner), anything wider is Large.
    CHECK(OrcaExt::classify_part_size(small_cut - 1., kBedArea, OrcaExt::kHighPartHeightMm) == OrcaExt::PartSizeBucket::MediumLarge);
    CHECK(OrcaExt::classify_part_size(small_cut + 1., kBedArea, 70.) == OrcaExt::PartSizeBucket::Large);
    CHECK(OrcaExt::classify_part_size(kBedArea, kBedArea, OrcaExt::kTallPartHeightMm - 0.01) == OrcaExt::PartSizeBucket::Large);

    // Tall wins at 100 mm, whatever the footprint.
    CHECK(OrcaExt::classify_part_size(10., kBedArea, OrcaExt::kTallPartHeightMm) == OrcaExt::PartSizeBucket::Tall);

    // A plate with no usable area, or no part, cannot be classified: the middle band keeps the
    // automation conservative instead of guessing an extreme.
    CHECK(OrcaExt::classify_part_size(small_cut, 0., 10.) == OrcaExt::PartSizeBucket::Medium);
    CHECK(OrcaExt::classify_part_size(0., kBedArea, 10.) == OrcaExt::PartSizeBucket::Medium);
}

// The six band edges are saved in AppConfig so they can be tuned without a rebuild; anything
// malformed, out of order or out of range must fall back to the defaults instead of mis-classifying.
TEST_CASE("Size bands round-trip through the AppConfig string and fall back safely", "[OrcaPlateAutomation]") {
    OrcaExt::PartSizeLimits defaults;
    CHECK(OrcaExt::serialize_part_size_limits(defaults) == "3,15;3,20,50,100");

    OrcaExt::PartSizeLimits custom;
    custom.fp_small_pct = 5.;
    custom.fp_mid_pct   = 10.;
    custom.h_thin_mm    = 2.;
    custom.h_mid_mm     = 10.;
    custom.h_high_mm    = 30.;
    custom.h_tall_mm    = 80.;
    CHECK(OrcaExt::serialize_part_size_limits(custom) == "5,10;2,10,30,80");

    const OrcaExt::PartSizeLimits parsed = OrcaExt::parse_part_size_limits("5,10;2,10,30,80");
    CHECK_THAT(parsed.fp_small_pct, WithinAbs(5., 1e-9));
    CHECK_THAT(parsed.fp_mid_pct, WithinAbs(10., 1e-9));
    CHECK_THAT(parsed.h_thin_mm, WithinAbs(2., 1e-9));
    CHECK_THAT(parsed.h_mid_mm, WithinAbs(10., 1e-9));
    CHECK_THAT(parsed.h_high_mm, WithinAbs(30., 1e-9));
    CHECK_THAT(parsed.h_tall_mm, WithinAbs(80., 1e-9));

    // Non-increasing footprint, non-increasing heights, missing parts, junk, out of range.
    for (const char *text : {"10,5;2,10,30,80", "5,10;10,2,30,80", "5,10;2,10,30,80,90",
                             "5,10", "junk", "0.01,10;2,10,30,80", "5,10;2,10,30,600"})
        CHECK_THAT(OrcaExt::parse_part_size_limits(text).h_thin_mm, WithinAbs(OrcaExt::kThinPartHeightMm, 1e-9));
}

TEST_CASE("Classification follows custom size bands", "[OrcaPlateAutomation]") {
    OrcaExt::PartSizeLimits limits;
    limits.fp_small_pct = 5.;
    limits.fp_mid_pct   = 10.;
    limits.h_thin_mm    = 2.;
    limits.h_mid_mm     = 10.;
    limits.h_high_mm    = 30.;
    limits.h_tall_mm    = 80.;

    const double pct4  = kBedArea * 0.04;
    const double pct6  = kBedArea * 0.06;
    const double pct11 = kBedArea * 0.11;

    CHECK(OrcaExt::classify_part_size(kBedArea, kBedArea, 1.9, limits) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(pct4, kBedArea, 2., limits) == OrcaExt::PartSizeBucket::Small);
    CHECK(OrcaExt::classify_part_size(pct6, kBedArea, 2., limits) == OrcaExt::PartSizeBucket::SmallMedium);
    CHECK(OrcaExt::classify_part_size(pct6, kBedArea, 10., limits) == OrcaExt::PartSizeBucket::Medium);
    CHECK(OrcaExt::classify_part_size(pct11, kBedArea, 10., limits) == OrcaExt::PartSizeBucket::MediumLarge);
    CHECK(OrcaExt::classify_part_size(pct4, kBedArea, 30., limits) == OrcaExt::PartSizeBucket::MediumLarge);
    CHECK(OrcaExt::classify_part_size(pct6, kBedArea, 30., limits) == OrcaExt::PartSizeBucket::Large);
    CHECK(OrcaExt::classify_part_size(pct4, kBedArea, 80., limits) == OrcaExt::PartSizeBucket::Tall);
}

// The popup's readout needs the part that decided the bucket, not just the bucket.
TEST_CASE("The plate's bucket reports the deciding part", "[OrcaPlateAutomation]") {
    const std::vector<std::pair<double, double>> parts {{kBedArea, 1.}, {kBedArea * 0.05, 60.}};
    double                                      height = 0.;
    double                                      pct    = 0.;
    const auto                                  bucket = OrcaExt::plate_part_bucket(parts, kBedArea, OrcaExt::PartSizeLimits {}, &height, &pct);
    CHECK(bucket == OrcaExt::PartSizeBucket::Large);
    CHECK_THAT(height, WithinAbs(60., 1e-9));
    CHECK_THAT(pct, WithinAbs(5., 1e-9));
}

// The bed temperature serves the whole plate, so the riskiest part decides: a first-layer test next
// to a real part takes the real part's bucket, and a tall tower outvotes a large flat patch.
TEST_CASE("The plate's bucket is the riskiest part on it", "[OrcaPlateAutomation]") {
    using Bucket = OrcaExt::PartSizeBucket;
    using Part   = std::pair<double, double>; // footprint, height

    const std::vector<Part> thin_only {{kBedArea, 1.}, {100., 0.2}};
    CHECK(OrcaExt::plate_part_bucket(thin_only, kBedArea) == Bucket::Small);

    const std::vector<Part> patch_and_medium {{kBedArea, 1.}, {kBedArea * 0.05, 30.}};
    CHECK(OrcaExt::plate_part_bucket(patch_and_medium, kBedArea) == Bucket::Medium);

    const std::vector<Part> patch_and_tower {{kBedArea, 2.}, {50., 120.}};
    CHECK(OrcaExt::plate_part_bucket(patch_and_tower, kBedArea) == Bucket::Tall);

    CHECK(OrcaExt::plate_part_bucket({}, kBedArea) == Bucket::Small);
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
