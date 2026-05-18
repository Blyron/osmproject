#pragma once
#include <unordered_map>
#include "../osm/OsmTypes.h"

struct XY { double x = 0.0; double y = 0.0; };

using NodeXYMap = std::unordered_map<OsmId, XY>;

class Projection {
public:
    // Initialise from bounding box; reference point is bbox centre
    Projection(double minLat, double maxLat, double minLon, double maxLon);

    // Project a single lat/lon to local metres
    XY project(double lat, double lon) const;

    // Build lookup table for every node in the map
    NodeXYMap projectAll(const std::unordered_map<OsmId, OsmNode>& nodes) const;

    double refLat() const { return m_refLat; }
    double refLon() const { return m_refLon; }

private:
    double m_refLat  = 0.0;
    double m_refLon  = 0.0;
    double m_cosLat  = 1.0;   // cos(refLat in radians)
    static constexpr double R = 6371000.0; // Earth radius metres
};
