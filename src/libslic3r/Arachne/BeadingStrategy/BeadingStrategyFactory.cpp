//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "BeadingStrategyFactory.hpp"

#include <boost/log/trivial.hpp>
#include <memory>
#include <utility>

#include "LimitedBeadingStrategy.hpp"
#include "WideningBeadingStrategy.hpp"
#include "DistributedBeadingStrategy.hpp"
#include "RedistributeBeadingStrategy.hpp"
#include "OuterWallInsetBeadingStrategy.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"
#include "libslic3r/NeoArachne/NeoArachneBeadingStrategy.hpp" // [ORCAPORT:PQ-3]

namespace Slic3r::Arachne {

BeadingStrategyPtr BeadingStrategyFactory::makeStrategy(const coord_t preferred_bead_width_outer,
                                                        const coord_t preferred_bead_width_inner,
                                                        const coord_t preferred_transition_length,
                                                        const float   transitioning_angle,
                                                        const bool    print_thin_walls,
                                                        const coord_t min_bead_width,
                                                        const coord_t min_feature_size,
                                                        const double  wall_split_middle_threshold,
                                                        const double  wall_add_middle_threshold,
                                                        const coord_t max_bead_count,
                                                        const coord_t outer_wall_offset,
                                                        const int     inward_distributed_center_wall_count,
                                                        const double  minimum_variable_line_ratio,
                                                        const bool    hybrid_edge_enabled,
                                                        const bool    hybrid_edge_pin_outer,
                                                        const bool    hybrid_edge_cap_widening,
                                                        const double  hybrid_edge_hysteresis_pct)
{
    // Handle a special case when there is just one external perimeter.
    // Because big differences in bead width for inner and other perimeters cause issues with current beading strategies.
    const coord_t      optimal_width = max_bead_count <= 2 ? preferred_bead_width_outer : preferred_bead_width_inner;
    BeadingStrategyPtr ret = std::make_unique<DistributedBeadingStrategy>(optimal_width, preferred_transition_length, transitioning_angle,
                                                                          wall_split_middle_threshold, wall_add_middle_threshold,
                                                                          inward_distributed_center_wall_count);

    BOOST_LOG_TRIVIAL(trace) << "Applying the Redistribute meta-strategy with outer-wall width = " << preferred_bead_width_outer << ", inner-wall width = " << preferred_bead_width_inner << ".";
    ret = std::make_unique<RedistributeBeadingStrategy>(preferred_bead_width_outer, minimum_variable_line_ratio, std::move(ret));

    if (print_thin_walls) {
        BOOST_LOG_TRIVIAL(trace) << "Applying the Widening Beading meta-strategy with minimum input width " << min_feature_size << " and minimum output width " << min_bead_width << ".";
        ret = std::make_unique<WideningBeadingStrategy>(std::move(ret), min_feature_size, min_bead_width);
    }
    // Orca: we allow negative outer_wall_offset here
    if (outer_wall_offset != 0) {
        BOOST_LOG_TRIVIAL(trace) << "Applying the OuterWallOffset meta-strategy with offset = " << outer_wall_offset << ".";
        ret = std::make_unique<OuterWallInsetBeadingStrategy>(outer_wall_offset, std::move(ret));
    }

    // [ORCAPORT:PQ-3] wrap with NeoArachneBeadingStrategy BEFORE Limited (Limited injects 0-width
    // marker walls other strategies must not touch). Pins outer width for bead_count 1/2, caps inner
    // widening, adds spatial hysteresis. No-op when hybrid_edge_enabled=false.
    if (hybrid_edge_enabled) {
        BOOST_LOG_TRIVIAL(trace) << "Applying the NeoArachne meta-strategy (pin outer + hysteresis " << hybrid_edge_hysteresis_pct << "%).";
        ret = std::make_unique<NeoArachne::NeoArachneBeadingStrategy>(preferred_bead_width_outer,
                                                                      hybrid_edge_hysteresis_pct,
                                                                      hybrid_edge_pin_outer,
                                                                      hybrid_edge_cap_widening,
                                                                      std::move(ret));
    }

    // Apply the LimitedBeadingStrategy last, since that adds a 0-width marker wall which other beading strategies shouldn't touch.
    BOOST_LOG_TRIVIAL(trace) << "Applying the Limited Beading meta-strategy with maximum bead count = " << max_bead_count << ".";
    ret = std::make_unique<LimitedBeadingStrategy>(max_bead_count, std::move(ret));
    return ret;
}
} // namespace Slic3r::Arachne
