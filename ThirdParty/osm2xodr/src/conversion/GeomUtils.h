#pragma once
#include <vector>
#include "../osm/OsmTypes.h"
#include "../xodr/XodrTypes.h"
#include "Projection.h"

namespace GeomUtils {

// Position and heading at a point along a geometry chain
struct PosHdg { double x, y, hdg; };

// Build a chain of line geometry elements from an ordered list of node IDs.
// Skips degenerate segments (length < minLength).
// Returns the total arc length via the last element's s + length.
std::vector<XodrGeometry> buildGeometryChain(
    const std::vector<OsmId>& nodeIds,
    const NodeXYMap&          nodeXY,
    double                    minLength = 0.01);

// Total length of a geometry chain
double chainLength(const std::vector<XodrGeometry>& chain);

// Heading in radians from (x1,y1) to (x2,y2).
// Convention: 0 = +X (east), increases counter-clockwise (standard math / OpenDRIVE)
double heading(double x1, double y1, double x2, double y2);

// Evaluate position and heading at arc length s along a piecewise-linear chain.
PosHdg evaluateChain(const std::vector<XodrGeometry>& chain, double s);

// Trim a geometry chain from the end by `dist` metres. Returns new endpoint.
PosHdg trimChainEnd(std::vector<XodrGeometry>& chain, double dist);

// Trim a geometry chain from the start by `dist` metres. Returns new startpoint.
PosHdg trimChainStart(std::vector<XodrGeometry>& chain, double dist);

} // namespace GeomUtils
