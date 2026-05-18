#include "OsmFilter.h"
#include <unordered_set>

static const std::unordered_set<std::string> DRIVABLE = {
    "motorway",      "motorway_link",
    "trunk",         "trunk_link",
    "primary",       "primary_link",
    "secondary",     "secondary_link",
    "tertiary",      "tertiary_link",
    "residential",
    "living_street",
    "service",
    "unclassified"
};

bool OsmFilter::isDrivable(const std::string& highwayType)
{
    return DRIVABLE.count(highwayType) > 0;
}

void OsmFilter::apply(OsmData& data)
{
    auto it = data.ways.begin();
    while (it != data.ways.end()) {
        const OsmWay& w = it->second;

        // Filter by highway type
        if (!isDrivable(w.highwayType())) {
            it = data.ways.erase(it);
            continue;
        }

        // Require at least 2 nodes
        if (w.nodeRefs.size() < 2) {
            it = data.ways.erase(it);
            continue;
        }

        // Remove ways that reference nodes outside the dataset
        bool allPresent = true;
        for (OsmId ref : w.nodeRefs) {
            if (data.nodes.find(ref) == data.nodes.end()) {
                allPresent = false;
                break;
            }
        }
        if (!allPresent) {
            it = data.ways.erase(it);
            continue;
        }

        ++it;
    }
}
