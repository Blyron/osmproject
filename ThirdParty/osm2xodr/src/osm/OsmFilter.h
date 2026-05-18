#pragma once
#include "OsmTypes.h"

class OsmFilter {
public:
    // Remove non-drivable ways (footway, cycleway, etc.) in-place.
    // Also removes ways whose nodes are absent from data.nodes.
    void apply(OsmData& data);

    static bool isDrivable(const std::string& highwayType);
};
