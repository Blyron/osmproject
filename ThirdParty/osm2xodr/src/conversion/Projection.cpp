#include "Projection.h"
#include <cmath>

static constexpr double PI = 3.14159265358979323846;
static constexpr double DEG2RAD = PI / 180.0;

Projection::Projection(double minLat, double maxLat, double minLon, double maxLon)
{
    m_refLat = (minLat + maxLat) * 0.5;
    m_refLon = (minLon + maxLon) * 0.5;
    m_cosLat = std::cos(m_refLat * DEG2RAD);
}

XY Projection::project(double lat, double lon) const
{
    XY p;
    p.x = (lon - m_refLon) * m_cosLat * R * DEG2RAD;
    p.y = (lat - m_refLat) * R * DEG2RAD;
    return p;
}

NodeXYMap Projection::projectAll(const std::unordered_map<OsmId, OsmNode>& nodes) const
{
    NodeXYMap result;
    result.reserve(nodes.size());
    for (const auto& [id, node] : nodes) {
        result[id] = project(node.lat, node.lon);
    }
    return result;
}
