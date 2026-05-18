#pragma once
#include "../osm/OsmTypes.h"
#include "../xodr/XodrTypes.h"
#include "Projection.h"

class RoadNetwork {
public:
    // Build the complete XODR network from filtered OSM data.
    XodrNetwork build(const OsmData& data, const NodeXYMap& nodeXY);

private:
    // ---- internal types ----------------------------------------------------

    struct RoadSegment {
        OsmId              sourceWayId = 0;
        std::vector<OsmId> nodes;   // ordered, includes both endpoints
        TagMap             tags;    // copied from parent way
    };

    // ---- stage methods -------------------------------------------------------

    // Step A+B: find intersection nodes and split ways into segments.
    // nodeXY is used to orient roundabout arcs to CCW (right-hand traffic).
    std::vector<RoadSegment> splitWays(const OsmData& data,
                                       const NodeXYMap& nodeXY) const;

    // Build lane section from OSM tags
    XodrLaneSection buildLaneSection(const TagMap& tags) const;

    // Map OSM highway type → XODR road type string + default speed (km/h)
    static std::string osmTypeToXodrType(const std::string& hw);
    static double      osmTypeToSpeed(const TagMap& tags);
    static double      laneWidthForType(const std::string& hw);
};
