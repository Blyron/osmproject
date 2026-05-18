#include "RoadNetwork.h"
#include "GeomUtils.h"
#include <algorithm>
#include <unordered_set>
#include <set>
#include <map>
#include <tuple>
#include <cmath>

// ============================================================================
//  Helpers
// ============================================================================

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int parseInt(const std::string& s, int defaultVal)
{
    if (s.empty()) return defaultVal;
    try { return std::stoi(s); } catch (...) { return defaultVal; }
}

// ============================================================================
//  Step A+B: intersection detection and way splitting
// ============================================================================

// Compute the signed area of a polygon given by ordered node IDs and their
// projected XY coordinates (shoelace formula).
// Positive = CCW, Negative = CW.
static double signedArea(const std::vector<OsmId>& nodeIds,
                         const NodeXYMap& nodeXY)
{
    double area = 0.0;
    const std::size_t n = nodeIds.size();
    for (std::size_t i = 0; i < n; ++i) {
        auto it1 = nodeXY.find(nodeIds[i]);
        auto it2 = nodeXY.find(nodeIds[(i + 1) % n]);
        if (it1 == nodeXY.end() || it2 == nodeXY.end()) continue;
        area += it1->second.x * it2->second.y;
        area -= it2->second.x * it1->second.y;
    }
    return area * 0.5;
}

std::vector<RoadNetwork::RoadSegment>
RoadNetwork::splitWays(const OsmData& data, const NodeXYMap& nodeXY) const
{
    // Step A: count per-node way references
    std::unordered_map<OsmId, int> nodeRefCount;
    nodeRefCount.reserve(data.nodes.size());

    for (const auto& [wid, way] : data.ways) {
        for (OsmId nid : way.nodeRefs) {
            nodeRefCount[nid]++;
        }
    }

    // Intersection = referenced by ≥2 ways, OR first/last node of any way
    std::unordered_set<OsmId> intersectionNodes;
    for (const auto& [nid, cnt] : nodeRefCount) {
        if (cnt >= 2) intersectionNodes.insert(nid);
    }
    for (const auto& [wid, way] : data.ways) {
        if (!way.nodeRefs.empty()) {
            intersectionNodes.insert(way.nodeRefs.front());
            intersectionNodes.insert(way.nodeRefs.back());
        }
    }

    // Step B: split each way at intersection nodes
    std::vector<RoadSegment> segments;
    segments.reserve(data.ways.size() * 2);

    for (const auto& [wid, way] : data.ways) {
        const auto& refs = way.nodeRefs;
        if (refs.size() < 2) continue;

        // Start with the declared node order
        std::vector<OsmId> ordered = refs;

        // oneway=-1: reverse node list, then treat as normal oneway
        if (way.isReversed()) {
            std::reverse(ordered.begin(), ordered.end());
        }

        // Roundabout orientation fix:
        // In right-hand traffic, vehicles travel CCW around the island.
        // OpenDRIVE right-side lanes (id < 0) are to the RIGHT of the
        // reference line direction.  For a CCW arc, the right side faces
        // outward (correct). For a CW arc, the right side faces inward
        // toward the island (wrong → lanes rendered on the wrong side).
        //
        // OSM roundabout data can be digitised in either direction, so we
        // enforce CCW by checking the signed area of the node polygon and
        // reversing if it is negative (CW).
        if (way.isRoundabout()) {
            double area = signedArea(ordered, nodeXY);
            if (area < 0.0) {
                std::reverse(ordered.begin(), ordered.end());
            }
        }

        std::vector<OsmId> current;
        current.push_back(ordered[0]);

        for (std::size_t i = 1; i < ordered.size(); ++i) {
            OsmId nid = ordered[i];
            current.push_back(nid);

            bool isLast       = (i == ordered.size() - 1);
            bool isSplitPoint = intersectionNodes.count(nid) && !isLast;

            if (isSplitPoint) {
                if (current.size() >= 2) {
                    segments.push_back({way.id, current, way.tags});
                }
                current.clear();
                current.push_back(nid);
            }
        }

        if (current.size() >= 2) {
            segments.push_back({way.id, current, way.tags});
        }
    }

    return segments;
}

// ============================================================================
//  Lane section builder
// ============================================================================

double RoadNetwork::laneWidthForType(const std::string& hw)
{
    if (hw == "motorway" || hw == "motorway_link") return 3.75;
    if (hw == "primary"  || hw == "secondary")     return 3.5;
    if (hw == "tertiary")                           return 3.25;
    if (hw == "residential")                        return 3.0;
    if (hw == "service"  || hw == "living_street")  return 2.75;
    return 3.25;
}

// Split a pipe-separated per-lane string into individual values.
// "yes|yes|designated" → {"yes", "yes", "designated"}
// "|designated" (leading empty) → {"", "designated"}
static std::vector<std::string> splitPipes(const std::string& s)
{
    if (s.empty()) return {};
    std::vector<std::string> result;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        std::size_t end = s.find('|', pos);
        if (end == std::string::npos) end = s.size();
        result.push_back(s.substr(pos, end - pos));
        pos = end + 1;
    }
    return result;
}

// Given parallel per-lane tag vectors (same length expected), classify each
// lane position as "driving", "restricted" (bus/taxi/psv designated), or
// "biking" (cycleway designated).
// access_vals: access:lanes — "no" at a position marks it restricted.
// bus_vals / taxi_vals / psv_vals: "designated" marks restricted.
// cycle_vals: "designated" marks biking.
static std::vector<std::string> classifyPerLane(
    const std::vector<std::string>& bus_vals,
    const std::vector<std::string>& taxi_vals,
    const std::vector<std::string>& psv_vals,
    const std::vector<std::string>& access_vals,
    const std::vector<std::string>& cycle_vals,
    int total)
{
    std::vector<std::string> types(total, "driving");
    for (int i = 0; i < total; ++i) {
        auto get = [&](const std::vector<std::string>& v) -> const std::string& {
            static const std::string empty;
            return (i < (int)v.size()) ? v[i] : empty;
        };
        if (get(cycle_vals) == "designated") {
            types[i] = "biking";
        } else if (get(bus_vals)    == "designated" ||
                   get(taxi_vals)   == "designated" ||
                   get(psv_vals)    == "designated" ||
                   get(access_vals) == "no") {
            types[i] = "restricted";
        }
    }
    return types;
}

XodrLaneSection RoadNetwork::buildLaneSection(const TagMap& tags) const
{
    XodrLaneSection section;
    section.s = 0.0;

    section.centerLane.id    = 0;
    section.centerLane.type  = "none";
    section.centerLane.level = false;

    auto tagVal = [&](const std::string& k) -> std::string {
        auto it = tags.find(k);
        return (it != tags.end()) ? it->second : "";
    };

    std::string hwType = tagVal("highway");
    double laneWidth   = laneWidthForType(hwType);

    bool isOneway = (tagVal("oneway") == "yes" || tagVal("oneway") == "1" ||
                     tagVal("junction") == "roundabout" ||
                     tagVal("junction") == "circular");

    // ---- Lane builders (shared) -------------------------------------------
    auto makeWidth = [&](double w) -> XodrLane::Width {
        return {0.0, w, 0.0, 0.0, 0.0};
    };
    auto makeLane = [&](int id, const std::string& type, double w) -> XodrLane {
        XodrLane l;
        l.id    = id;
        l.type  = type;
        l.level = false;
        l.widths.push_back(makeWidth(w));
        return l;
    };

    // ========================================================================
    //  PATH A: per-lane tags present
    //  (bus:lanes, taxi:lanes, psv:lanes, access:lanes, cycleway:lanes)
    //
    //  OSM meaning: lanes=N is the TOTAL physical count (driving + special).
    //  The pipe-separated values classify each lane position individually.
    //  Multiple tags (bus + taxi) on the same lane are deduplicated — only
    //  ONE restricted lane is emitted per position regardless of how many
    //  access modifiers apply.
    // ========================================================================

    // Directional variants — forward/backward may be tagged separately
    std::string busF  = tagVal("bus:lanes:forward");
    std::string busB  = tagVal("bus:lanes:backward");
    std::string busA  = tagVal("bus:lanes");        // undirected

    std::string taxiF = tagVal("taxi:lanes:forward");
    std::string taxiB = tagVal("taxi:lanes:backward");
    std::string taxiA = tagVal("taxi:lanes");

    std::string psvF  = tagVal("psv:lanes:forward");
    std::string psvB  = tagVal("psv:lanes:backward");
    std::string psvA  = tagVal("psv:lanes");

    std::string accF  = tagVal("access:lanes:forward");
    std::string accB  = tagVal("access:lanes:backward");
    std::string accA  = tagVal("access:lanes");

    std::string cycF  = tagVal("cycleway:lanes:forward");
    std::string cycB  = tagVal("cycleway:lanes:backward");
    std::string cycA  = tagVal("cycleway:lanes");

    bool hasPerLane = !busA.empty() || !taxiA.empty() || !psvA.empty() ||
                      !accA.empty() || !cycA.empty()  ||
                      !busF.empty() || !taxiF.empty() || !psvF.empty() ||
                      !accF.empty() || !cycF.empty()  ||
                      !busB.empty() || !taxiB.empty() || !psvB.empty() ||
                      !accB.empty() || !cycB.empty();

    if (hasPerLane) {
        // For oneway or undirected tags, merge forward/undirected.
        // For bidirectional with explicit fwd/bwd tags, handle each side.

        auto buildSide = [&](const std::string& bus,  const std::string& taxi,
                              const std::string& psv,  const std::string& acc,
                              const std::string& cyc,
                              int lanesTag) -> std::vector<std::string>
        {
            auto bv = splitPipes(bus);
            auto tv = splitPipes(taxi);
            auto pv = splitPipes(psv);
            auto av = splitPipes(acc);
            auto cv = splitPipes(cyc);

            // Total = max of pipe count and the declared lanes= tag
            int pipeMax = (int)std::max({bv.size(), tv.size(), pv.size(),
                                         av.size(), cv.size()});
            int total = std::max(lanesTag, pipeMax);
            if (total <= 0) total = 1;

            return classifyPerLane(bv, tv, pv, av, cv, total);
        };

        int lanesTag = parseInt(tagVal("lanes"), 0);

        if (isOneway) {
            // Use forward tags if available, otherwise undirected
            std::string b = !busF.empty()  ? busF  : busA;
            std::string t = !taxiF.empty() ? taxiF : taxiA;
            std::string p = !psvF.empty()  ? psvF  : psvA;
            std::string a = !accF.empty()  ? accF  : accA;
            std::string c = !cycF.empty()  ? cycF  : cycA;

            auto types = buildSide(b, t, p, a, c, lanesTag);

            // OSM pipe order: leftmost lane first when traveling in road direction.
            // In right-hand traffic, leftmost = closest to center = XODR id=-1 (innermost).
            // So iterate forward: position 0 → id=-1, position N-1 → id=-N (outermost).
            int rId = -1;
            for (int i = 0; i < (int)types.size(); ++i) {
                double w = (types[i] == "biking") ? 1.5 : laneWidth;
                section.rightLanes.push_back(makeLane(rId--, types[i], w));
            }
        } else {
            // Bidirectional with per-lane tags
            // If directional variants exist, use them; otherwise split undirected tags
            bool hasDirectional = !busF.empty() || !taxiF.empty() || !psvF.empty() ||
                                  !accF.empty() || !cycF.empty()  ||
                                  !busB.empty() || !taxiB.empty() || !psvB.empty() ||
                                  !accB.empty() || !cycB.empty();

            int fwdTotal = parseInt(tagVal("lanes:forward"),  0);
            int bwdTotal = parseInt(tagVal("lanes:backward"), 0);

            if (!hasDirectional) {
                // Undirected per-lane tags on bidirectional road.
                // Pipe order: leftmost first = backward lanes first, then forward.
                auto bv = splitPipes(busA);
                auto tv = splitPipes(taxiA);
                auto pv = splitPipes(psvA);
                auto av = splitPipes(accA);
                auto cv = splitPipes(cycA);

                int pipeMax = (int)std::max({bv.size(), tv.size(), pv.size(),
                                             av.size(), cv.size()});
                int total = std::max(lanesTag, pipeMax);
                if (total <= 0) total = isOneway ? 1 : 2;

                if (fwdTotal <= 0 && bwdTotal <= 0) {
                    fwdTotal = total / 2;
                    bwdTotal = total - fwdTotal;
                } else if (fwdTotal <= 0) {
                    fwdTotal = total - bwdTotal;
                } else if (bwdTotal <= 0) {
                    bwdTotal = total - fwdTotal;
                }

                auto types = classifyPerLane(bv, tv, pv, av, cv, total);

                // OSM pipe order on a bidirectional road (traveling in forward dir):
                //   position 0 = leftmost = the backward lane furthest from center
                //   position bwdTotal-1 = backward lane closest to center → XODR left id=+1
                //   position bwdTotal   = forward lane closest to center  → XODR right id=-1
                //   last position       = forward lane furthest from center → outermost right

                // Backward (left side): positions 0..bwdTotal-1
                // Position bwdTotal-1 is closest to center → XODR id=+1 (innermost left)
                // So iterate in reverse order to assign id=+1 to the innermost
                int lId = 1;
                for (int i = bwdTotal - 1; i >= 0; --i) {
                    double w = (types[i] == "biking") ? 1.5 : laneWidth;
                    section.leftLanes.push_back(makeLane(lId++, types[i], w));
                }
                // Forward (right side): positions bwdTotal..end
                // Position bwdTotal is closest to center → XODR id=-1 (innermost right)
                // Iterate forward
                int rId = -1;
                for (int i = bwdTotal; i < bwdTotal + fwdTotal && i < total; ++i) {
                    double w = (types[i] == "biking") ? 1.5 : laneWidth;
                    section.rightLanes.push_back(makeLane(rId--, types[i], w));
                }
            } else {
                // Directional per-lane tags
                if (fwdTotal <= 0) fwdTotal = lanesTag / 2;
                if (bwdTotal <= 0) bwdTotal = lanesTag - fwdTotal;

                // Backward (left side): use backward tags
                // The backward tags are ordered from the lane furthest from center
                // (leftmost when going backward = rightmost when going forward) to innermost.
                // So last position → id=+1 (innermost left). Iterate in reverse.
                auto bwdTypes = buildSide(!busB.empty() ? busB : busA,
                                          !taxiB.empty() ? taxiB : taxiA,
                                          !psvB.empty()  ? psvB  : psvA,
                                          !accB.empty()  ? accB  : accA,
                                          !cycB.empty()  ? cycB  : cycA,
                                          bwdTotal);
                int lId = 1;
                for (int i = (int)bwdTypes.size() - 1; i >= 0; --i) {
                    double w = (bwdTypes[i] == "biking") ? 1.5 : laneWidth;
                    section.leftLanes.push_back(makeLane(lId++, bwdTypes[i], w));
                }

                // Forward (right side): use forward tags
                // Position 0 = leftmost when going forward = closest to center = id=-1.
                // Iterate forward.
                auto fwdTypes = buildSide(!busF.empty() ? busF : busA,
                                          !taxiF.empty() ? taxiF : taxiA,
                                          !psvF.empty()  ? psvF  : psvA,
                                          !accF.empty()  ? accF  : accA,
                                          !cycF.empty()  ? cycF  : cycA,
                                          fwdTotal);
                int rId = -1;
                for (int i = 0; i < (int)fwdTypes.size(); ++i) {
                    double w = (fwdTypes[i] == "biking") ? 1.5 : laneWidth;
                    section.rightLanes.push_back(makeLane(rId--, fwdTypes[i], w));
                }
            }
        }

        return section;
    }

    // ========================================================================
    //  PATH B: count tags only (lanes:bus=N, lanes:bicycle=N, lanes:taxi=N, …)
    //
    //  OSM meaning: lanes=N counts ONLY driving lanes.
    //  Special lanes are ADDITIVE on top of lanes=N.
    // ========================================================================

    // Count-based special lanes
    auto countTag = [&](const std::string& k) -> int {
        return parseInt(tagVal(k), 0);
    };

    // Bus/psv count tags (additive)
    int busFwd  = countTag("lanes:bus:forward")  + countTag("lanes:psv:forward");
    int busBwd  = countTag("lanes:bus:backward") + countTag("lanes:psv:backward");
    int busAll  = countTag("lanes:bus")          + countTag("lanes:psv");
    if (busAll > 0 && busFwd == 0 && busBwd == 0) {
        if (isOneway) { busFwd = busAll; }
        else          { busFwd = busAll / 2 + (busAll % 2); busBwd = busAll / 2; }
    }

    // Taxi count tags (additive, treated as restricted)
    int taxiFwd = countTag("lanes:taxi:forward");
    int taxiBwd = countTag("lanes:taxi:backward");
    int taxiAll = countTag("lanes:taxi");
    if (taxiAll > 0 && taxiFwd == 0 && taxiBwd == 0) {
        if (isOneway) { taxiFwd = taxiAll; }
        else          { taxiFwd = taxiAll / 2 + (taxiAll % 2); taxiBwd = taxiAll / 2; }
    }

    // HOV count tags
    int hovFwd  = countTag("lanes:hov:forward");
    int hovBwd  = countTag("lanes:hov:backward");
    int hovAll  = countTag("lanes:hov");
    if (hovAll > 0 && hovFwd == 0 && hovBwd == 0) {
        if (isOneway) { hovFwd = hovAll; }
        else          { hovFwd = hovAll / 2 + (hovAll % 2); hovBwd = hovAll / 2; }
    }

    // Bicycle count tags (additive)
    int bikeFwd = countTag("lanes:bicycle:forward");
    int bikeBwd = countTag("lanes:bicycle:backward");
    int bikeAll = countTag("lanes:bicycle");
    if (bikeAll > 0 && bikeFwd == 0 && bikeBwd == 0) {
        if (isOneway) { bikeFwd = bikeAll; }
        else          { bikeFwd = bikeAll / 2 + (bikeAll % 2); bikeBwd = bikeAll / 2; }
    }

    // Driving lanes — from lanes= (driving only) or lanes:forward/backward
    int fwdDrive = countTag("lanes:forward");
    int bwdDrive = countTag("lanes:backward");
    int drvTotal = parseInt(tagVal("lanes"), 0);

    if (fwdDrive > 0 || bwdDrive > 0) {
        if (fwdDrive <= 0) fwdDrive = 1;
        if (!isOneway && bwdDrive <= 0) bwdDrive = 1;
    } else {
        if (drvTotal <= 0) drvTotal = isOneway ? 1 : 2;
        drvTotal = clampInt(drvTotal, 1, 8);
        if (isOneway) {
            fwdDrive = drvTotal;
        } else {
            fwdDrive = drvTotal / 2;
            bwdDrive = drvTotal - fwdDrive;
        }
    }

    fwdDrive = clampInt(fwdDrive, 1, 8);
    if (!isOneway) bwdDrive = clampInt(bwdDrive, 1, 8);

    // Assemble: driving innermost, then restricted (bus/hov/taxi), then biking
    if (isOneway) {
        int id = -1;
        for (int i = 0; i < fwdDrive;                     ++i) section.rightLanes.push_back(makeLane(id--, "driving",    laneWidth));
        for (int i = 0; i < hovFwd + taxiFwd + busFwd;    ++i) section.rightLanes.push_back(makeLane(id--, "restricted", laneWidth));
        for (int i = 0; i < bikeFwd;                      ++i) section.rightLanes.push_back(makeLane(id--, "biking",     1.5));
    } else {
        int lId = 1;
        for (int i = 0; i < bwdDrive;                     ++i) section.leftLanes.push_back(makeLane(lId++, "driving",    laneWidth));
        for (int i = 0; i < hovBwd + taxiBwd + busBwd;    ++i) section.leftLanes.push_back(makeLane(lId++, "restricted", laneWidth));
        for (int i = 0; i < bikeBwd;                      ++i) section.leftLanes.push_back(makeLane(lId++, "biking",     1.5));

        int rId = -1;
        for (int i = 0; i < fwdDrive;                     ++i) section.rightLanes.push_back(makeLane(rId--, "driving",    laneWidth));
        for (int i = 0; i < hovFwd + taxiFwd + busFwd;    ++i) section.rightLanes.push_back(makeLane(rId--, "restricted", laneWidth));
        for (int i = 0; i < bikeFwd;                      ++i) section.rightLanes.push_back(makeLane(rId--, "biking",     1.5));
    }

    return section;
}

// ============================================================================
//  Road type / speed mapping
// ============================================================================

std::string RoadNetwork::osmTypeToXodrType(const std::string& hw)
{
    if (hw == "motorway" || hw == "motorway_link") return "motorway";
    if (hw == "trunk"    || hw == "trunk_link")    return "rural";
    if (hw == "service"  || hw == "living_street") return "lowSpeed";
    return "town";
}

double RoadNetwork::osmTypeToSpeed(const TagMap& tags)
{
    auto it = tags.find("maxspeed");
    if (it != tags.end()) {
        try { return std::stod(it->second); } catch (...) {}
    }

    auto hwIt = tags.find("highway");
    std::string hw = (hwIt != tags.end()) ? hwIt->second : "";

    if (hw == "motorway")      return 120.0;
    if (hw == "trunk")         return 100.0;
    if (hw == "primary")       return 70.0;
    if (hw == "secondary")     return 60.0;
    if (hw == "tertiary")      return 50.0;
    if (hw == "residential")   return 30.0;
    if (hw == "living_street") return 20.0;
    if (hw == "service")       return 20.0;
    return 50.0;
}

// ============================================================================
//  Main build method
// ============================================================================

XodrNetwork RoadNetwork::build(const OsmData& data, const NodeXYMap& nodeXY)
{
    XodrNetwork net;
    net.refLat = (data.minLat + data.maxLat) * 0.5;
    net.refLon = (data.minLon + data.maxLon) * 0.5;

    // Split ways into road segments (nodeXY needed for roundabout orientation)
    std::vector<RoadSegment> segments = splitWays(data, nodeXY);

    // Assign provisional road IDs (one per segment, some will be unused)
    std::vector<XodrId> segToRoadId(segments.size());
    XodrId nextRoadId = 0;
    for (auto& id : segToRoadId) id = nextRoadId++;

    // ---- Quick lane count for mismatch detection (right-side lanes only) ----
    // Used to upgrade degree-2 through-nodes with different lane counts to
    // virtual junctions so we can generate explicit merge/split lane links.
    auto quickRightLaneCount = [](const TagMap& tags) -> int {
        auto tagVal = [&](const std::string& k) -> std::string {
            auto it = tags.find(k);
            return (it != tags.end()) ? it->second : "";
        };
        bool oneway = (tagVal("oneway") == "yes" || tagVal("oneway") == "1" ||
                       tagVal("junction") == "roundabout" ||
                       tagVal("junction") == "circular");
        int total = parseInt(tagVal("lanes"), 0);
        if (total <= 0) total = oneway ? 1 : 2;
        total = clampInt(total, 1, 8);

        // Include additive count-tag lanes (lanes:bus, lanes:psv, etc.)
        auto countTag = [&](const std::string& k) -> int { return parseInt(tagVal(k), 0); };
        int extra = countTag("lanes:bus") + countTag("lanes:psv") +
                    countTag("lanes:taxi") + countTag("lanes:hov") +
                    countTag("lanes:bicycle");
        if (!oneway) extra = 0; // additive tags are per direction, skip for bidi estimate

        if (oneway) return total + extra;

        // Bidirectional: forward side
        int fwd = parseInt(tagVal("lanes:forward"), 0);
        if (fwd <= 0) fwd = total / 2;
        return fwd;
    };

    // ---- Build topology (nodeToEndpoints) ----
    struct SegEndpoint {
        int  segIdx  = -1;
        bool isStart = true; // true=road STARTS at this node; false=road ENDS here
    };
    std::unordered_map<OsmId, std::vector<SegEndpoint>> nodeToEndpoints;
    nodeToEndpoints.reserve(segments.size() * 2);

    for (int i = 0; i < (int)segments.size(); ++i) {
        const auto& seg = segments[i];
        nodeToEndpoints[seg.nodes.front()].push_back({i, true});
        nodeToEndpoints[seg.nodes.back() ].push_back({i, false});
    }

    // Junction node = degree > 2, OR degree-2 with mismatched lane counts
    // (lane count transitions need explicit lane links just like real junctions)
    std::unordered_set<OsmId> junctionOsmNodes;
    for (const auto& [nid, eps] : nodeToEndpoints) {
        if (eps.size() > 2) {
            junctionOsmNodes.insert(nid);
        } else if (eps.size() == 2) {
            int lanesA = quickRightLaneCount(segments[eps[0].segIdx].tags);
            int lanesB = quickRightLaneCount(segments[eps[1].segIdx].tags);
            if (lanesA != lanesB) {
                junctionOsmNodes.insert(nid);
            }
        }
    }

    // Assign junction XODR IDs
    XodrId nextJunctionId = 0;
    std::unordered_map<OsmId, XodrId> juncNodeToId;
    for (OsmId jnid : junctionOsmNodes) {
        juncNodeToId[jnid] = nextJunctionId++;
        XodrJunction junc;
        junc.id   = juncNodeToId[jnid];
        junc.name = "junction_" + std::to_string(jnid);
        net.junctions.push_back(std::move(junc));
    }

    // ========================================================================
    //  PASS 1: Build XodrRoad objects (no predecessor/successor yet)
    // ========================================================================

    // segToRoadVecIdx[si] = index in net.roads, or -1 if segment is degenerate
    std::vector<int> segToRoadVecIdx(segments.size(), -1);

    for (int si = 0; si < (int)segments.size(); ++si) {
        const RoadSegment& seg = segments[si];

        std::vector<XodrGeometry> geoms =
            GeomUtils::buildGeometryChain(seg.nodes, nodeXY);
        if (geoms.empty()) continue;

        double length = GeomUtils::chainLength(geoms);

        XodrRoad road;
        road.id              = segToRoadId[si];
        road.sourceWayId     = seg.sourceWayId;
        road.startNodeOsmId  = seg.nodes.front();
        road.endNodeOsmId    = seg.nodes.back();
        road.length          = length;
        road.geometries      = std::move(geoms);
        road.laneSections.push_back(buildLaneSection(seg.tags));
        road.junctionId      = -1;

        // Road type record
        {
            auto hwIt = seg.tags.find("highway");
            std::string hw = (hwIt != seg.tags.end()) ? hwIt->second : "";
            XodrRoadTypeRecord rt;
            rt.s        = 0.0;
            rt.type     = osmTypeToXodrType(hw);
            rt.maxSpeed = osmTypeToSpeed(seg.tags);
            road.roadTypes.push_back(rt);
        }

        // Road name
        {
            auto nameIt = seg.tags.find("name");
            road.name = (nameIt != seg.tags.end())
                        ? nameIt->second
                        : ("road_" + std::to_string(road.id));
        }

        segToRoadVecIdx[si] = (int)net.roads.size();
        net.roads.push_back(std::move(road));
    }

    // ========================================================================
    //  PASS 2: Wire predecessor / successor links
    // ========================================================================

    for (int si = 0; si < (int)segments.size(); ++si) {
        int vidx = segToRoadVecIdx[si];
        if (vidx < 0) continue;

        XodrRoad& road      = net.roads[vidx];
        const auto& seg     = segments[si];

        // Wire one end (predecessor if isPredecessor=true, else successor)
        auto wireLink = [&](OsmId nodeId, bool isPredecessor) {
            XodrRoadLink link;

            // Is this node a junction?
            auto juncIt = juncNodeToId.find(nodeId);
            if (juncIt != juncNodeToId.end()) {
                link.elementType  = XodrRoadLink::ElementType::Junction;
                link.elementId    = juncIt->second;
                link.contactPoint = XodrRoadLink::ContactPoint::None;
            } else {
                // Degree-2 through-node: find the other segment
                auto epIt = nodeToEndpoints.find(nodeId);
                if (epIt == nodeToEndpoints.end()) return;

                XodrId otherRoadId = -1;
                XodrRoadLink::ContactPoint cp = XodrRoadLink::ContactPoint::Start;

                for (const auto& ep : epIt->second) {
                    if (ep.segIdx == si) continue;
                    // Only link to segments that actually produced roads
                    if (segToRoadVecIdx[ep.segIdx] < 0) continue;
                    otherRoadId = segToRoadId[ep.segIdx];
                    cp = ep.isStart
                         ? XodrRoadLink::ContactPoint::Start
                         : XodrRoadLink::ContactPoint::End;
                    break;
                }
                if (otherRoadId < 0) return;

                link.elementType  = XodrRoadLink::ElementType::Road;
                link.elementId    = otherRoadId;
                link.contactPoint = cp;
            }

            if (isPredecessor)
                road.predecessor = link;
            else
                road.successor   = link;
        };

        wireLink(seg.nodes.front(), true);   // predecessor
        wireLink(seg.nodes.back(),  false);  // successor
    }

    // ========================================================================
    //  PASS 3: Build junction connections with correct lane links
    // ========================================================================

    // Lane info per road — counts only "driving" lanes for junction lane links.
    // Bus, restricted and biking lanes do not participate in junction connections.
    struct LaneInfo {
        int right = 1; // driving lanes on right side (negative IDs, forward direction)
        int left  = 0; // driving lanes on left side  (positive IDs, backward direction)
    };
    std::unordered_map<XodrId, LaneInfo> laneInfoMap;
    std::unordered_set<XodrId> validRoadIds;

    for (const auto& road : net.roads) {
        validRoadIds.insert(road.id);
        if (!road.laneSections.empty()) {
            LaneInfo li;
            // Count ALL physical lanes (driving + restricted + biking) so that
            // bus/taxi/restricted lanes are included in junction lane links.
            li.right = (int)road.laneSections[0].rightLanes.size();
            li.left  = (int)road.laneSections[0].leftLanes.size();
            if (li.right == 0) li.right = 1; // always at least 1 for connectivity
            laneInfoMap[road.id] = li;
        }
    }

    // Track which roads are roundabout segments (for outer-first exit mapping)
    std::unordered_set<XodrId> roundaboutRoadIds;
    for (int si = 0; si < (int)segments.size(); ++si) {
        if (segToRoadVecIdx[si] < 0) continue;
        auto jt = segments[si].tags.find("junction");
        if (jt != segments[si].tags.end() &&
            (jt->second == "roundabout" || jt->second == "circular")) {
            roundaboutRoadIds.insert(segToRoadId[si]);
        }
    }

    // Pending connection specs: keyed by (incomingRoad, connectingRoad, contactPoint)
    // to group all lane links for the same road pair into one connection element
    using CP = XodrJunctionConnection::ContactPoint;
    using LaneLink = XodrJunctionConnection::LaneLink;
    struct ConnSpec {
        XodrId incomingRoad   = -1;
        XodrId connectingRoad = -1;
        XodrJunctionConnection::ContactPoint contactPoint =
            XodrJunctionConnection::ContactPoint::Start;
        std::vector<LaneLink> laneLinks;
    };

    XodrId connId = 0;

    for (const auto& [jnid, jxid] : juncNodeToId) {
        // Find the junction object
        XodrJunction* juncPtr = nullptr;
        for (auto& j : net.junctions) {
            if (j.id == jxid) { juncPtr = &j; break; }
        }
        if (!juncPtr) continue;

        const auto& eps = nodeToEndpoints[jnid];

        // Collect valid endpoints at this junction
        struct EP {
            XodrId roadId;
            bool   isStart; // true = road starts at J (outgoing in +s dir)
                            // false = road ends at J   (incoming in +s dir)
        };
        std::vector<EP> validEps;
        for (const auto& ep : eps) {
            XodrId rid = segToRoadId[ep.segIdx];
            if (validRoadIds.count(rid))
                validEps.push_back({rid, ep.isStart});
        }

        // Accumulate lane links per (incomingRoad, connectingRoad, cp) key
        std::map<std::tuple<XodrId, XodrId, int>, ConnSpec> connMap;

        auto addLaneLink = [&](XodrId inId, XodrId outId, CP cp,
                                int fromLane, int toLane) {
            int cpKey = (cp == CP::Start) ? 0 : 1;
            auto key  = std::make_tuple(inId, outId, cpKey);
            auto& spec = connMap[key];
            spec.incomingRoad   = inId;
            spec.connectingRoad = outId;
            spec.contactPoint   = cp;
            spec.laneLinks.push_back({fromLane, toLane});
        };

        // --- Directional connection generation ---
        //
        // incomingRoad = road that ENDS at J (isStart=false), i.e. traffic arrives
        //                in the +s direction via right lanes (-1, -2, ...).
        //
        // connectingRoad exits J in two ways:
        //
        //   contactPoint=Start: road starts at J (isStart=true)
        //                       → exits via right lanes (-1, -2, ...) in +s direction
        //
        //   contactPoint=End:   road ends at J (isStart=false) AND is bidirectional
        //                       → exits via left lanes (+1, +2, ...) in -s direction
        //
        // We do NOT use bidirectional roads that START at J as incomingRoad (that
        // would require traffic to travel in the -s direction, which is rendered
        // as "going backwards" in OpenDRIVE viewers).

        // Collect incoming roads (end at J) and exit roads (start at J)
        std::vector<EP> incomingEps, exitEps;
        for (const auto& ep : validEps) {
            if (!ep.isStart) incomingEps.push_back(ep);
            else             exitEps.push_back(ep);
        }

        // --- Perfect-partition fork detection ---
        //
        // When exactly ONE road ends at J (a true fork/split), and the total lane
        // count across all exits exactly equals the incoming lane count, we can
        // partition lanes uniquely: innermost lanes → widest exit, outer lanes → narrower exits.
        // This avoids every incoming lane being routed to every exit (which is wrong
        // for off-ramp scenarios like a 4-lane road forking into 3+1 lanes).
        //
        // For forks where sum_exits > inRight (regular T/X intersections),
        // use the standard all-to-all approach so all lanes can turn either way.
        //
        auto applyForkPartition = [&](const EP& epIn) -> bool {
            // Roundabouts need outer-first exit mapping, not fork partition
            if (roundaboutRoadIds.count(epIn.roadId)) return false;

            auto inIt = laneInfoMap.find(epIn.roadId);
            if (inIt == laneInfoMap.end()) return false;
            int inRight = inIt->second.right;

            // Sum exit right-lane counts
            int sumExits = 0;
            for (const auto& epOut : exitEps)
                if (auto it = laneInfoMap.find(epOut.roadId); it != laneInfoMap.end())
                    sumExits += it->second.right;

            if (sumExits != inRight) return false; // not a perfect partition

            // Sort exits by lane count DESC (innermost lanes go to widest road)
            std::vector<EP> sortedExits = exitEps;
            std::sort(sortedExits.begin(), sortedExits.end(),
                      [&](const EP& a, const EP& b) {
                          int la = laneInfoMap.count(a.roadId) ? laneInfoMap.at(a.roadId).right : 0;
                          int lb = laneInfoMap.count(b.roadId) ? laneInfoMap.at(b.roadId).right : 0;
                          return la > lb;
                      });

            int offset = 1; // next innermost unassigned incoming lane index
            for (const auto& epOut : sortedExits) {
                auto outIt = laneInfoMap.find(epOut.roadId);
                if (outIt == laneInfoMap.end()) continue;
                int outRight = outIt->second.right;
                int n = std::min(inRight - offset + 1, outRight);
                if (n <= 0) break;
                for (int i = 0; i < n; ++i)
                    addLaneLink(epIn.roadId, epOut.roadId, CP::Start, -(offset + i), -(i + 1));
                offset += n;
            }
            return true;
        };

        // Single-incoming fork: try perfect partition first
        bool usedPartition = false;
        if (incomingEps.size() == 1 && !exitEps.empty()) {
            usedPartition = applyForkPartition(incomingEps[0]);
        }

        if (!usedPartition) {
            // Detect the widest incoming road at this junction.
            // Narrower incoming roads are "merge" roads that should connect
            // to the outermost lanes of the exit (right-side incorporation).
            int maxIncomingRight = 0;
            for (const auto& epIn : incomingEps) {
                auto inIt = laneInfoMap.find(epIn.roadId);
                if (inIt != laneInfoMap.end())
                    maxIncomingRight = std::max(maxIncomingRight, inIt->second.right);
            }

            for (const auto& epIn : incomingEps) {
                auto inIt = laneInfoMap.find(epIn.roadId);
                if (inIt == laneInfoMap.end()) continue;
                int inRight = inIt->second.right;
                bool isMergeRoad = (inRight < maxIncomingRight);

                for (const auto& epOut : validEps) {
                    if (epOut.roadId == epIn.roadId) continue; // no self-loops

                    auto outIt = laneInfoMap.find(epOut.roadId);
                    if (outIt == laneInfoMap.end()) continue;
                    int outRight = outIt->second.right;
                    int outLeft  = outIt->second.left;

                    bool inIsRoundabout = roundaboutRoadIds.count(epIn.roadId) > 0;

                    // Exit via road's START (contactPoint=Start): right → right
                    if (epOut.isStart) {
                        if (inIsRoundabout && inRight > outRight) {
                            // Roundabout exit: only outermost lanes connect
                            // e.g. inRight=2, outRight=1 → lane -2 maps to -1
                            for (int i = 1; i <= outRight; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::Start,
                                            -(inRight - outRight + i), -i);
                        } else if (isMergeRoad && inRight < outRight) {
                            // Merge from the right: narrower road joins wider road
                            // at outermost lanes. e.g. inRight=1, outRight=3 → -1 maps to -3
                            for (int i = 1; i <= inRight; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::Start,
                                            -i, -(outRight - inRight + i));
                        } else {
                            int n = std::min(inRight, outRight);
                            for (int i = 1; i <= n; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::Start, -i, -i);
                            // Merge: extra incoming lanes → outermost connecting lane
                            for (int i = n + 1; i <= inRight; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::Start, -i, -n);
                        }
                    }

                    // Exit via road's END going backward (contactPoint=End, bidi only)
                    // right (arriving) → left (departing in -s direction)
                    if (!epOut.isStart && outLeft > 0) {
                        if (inIsRoundabout && inRight > outLeft) {
                            // Roundabout exit via backward road: only outermost lanes
                            for (int i = 1; i <= outLeft; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::End,
                                            -(inRight - outLeft + i), i);
                        } else if (isMergeRoad && inRight < outLeft) {
                            // Merge from the right via backward road
                            for (int i = 1; i <= inRight; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::End,
                                            -i, (outLeft - inRight + i));
                        } else {
                            int n = std::min(inRight, outLeft);
                            for (int i = 1; i <= n; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::End, -i, i);
                            // Merge: extra incoming lanes → outermost backward lane
                            for (int i = n + 1; i <= inRight; ++i)
                                addLaneLink(epIn.roadId, epOut.roadId, CP::End, -i, n);
                        }
                    }
                }
            }
        }

        // Emit connections from accumulated specs
        for (auto& [key, spec] : connMap) {
            if (spec.laneLinks.empty()) continue;
            XodrJunctionConnection conn;
            conn.connectionId   = connId++;
            conn.incomingRoad   = spec.incomingRoad;
            conn.connectingRoad = spec.connectingRoad;
            conn.contactPoint   = spec.contactPoint;
            conn.laneLinks      = std::move(spec.laneLinks);
            juncPtr->connections.push_back(std::move(conn));
        }
    }

    // ========================================================================
    //  PASS 4: Create junction connecting roads (fills visual gaps)
    // ========================================================================
    //
    // For each junction connection we:
    //   1. Trim the incoming and outgoing roads back from the junction node
    //   2. Create a short connector road that bridges the gap
    //   3. Update the junction connection to reference the connector road

    // 4a: Build a road-ID → index map for fast lookup
    std::unordered_map<XodrId, int> roadIdToIdx;
    for (int i = 0; i < (int)net.roads.size(); ++i)
        roadIdToIdx[net.roads[i].id] = i;

    // 4b: Compute total road half-width at each end (for trim distance)
    auto roadHalfWidth = [&](const XodrRoad& road) -> double {
        if (road.laneSections.empty()) return 3.5;
        const auto& ls = road.laneSections[0];
        double w = 0.0;
        for (const auto& lane : ls.rightLanes)
            if (!lane.widths.empty()) w += lane.widths[0].a;
        for (const auto& lane : ls.leftLanes)
            if (!lane.widths.empty()) w += lane.widths[0].a;
        return std::max(w * 0.5, 2.0);
    };

    // 4c: Accumulate trim distance per road end from each junction it touches
    //     Key: (roadId, side)  where side: 0=start, 1=end
    struct TrimInfo {
        double startTrim = 0.0;
        double endTrim   = 0.0;
    };
    std::unordered_map<XodrId, TrimInfo> roadTrims;

    for (const auto& junc : net.junctions) {
        // Find max half-width of all roads at this junction
        double maxHW = 3.0;
        std::set<XodrId> roadsAtJunc;
        for (const auto& conn : junc.connections) {
            roadsAtJunc.insert(conn.incomingRoad);
            roadsAtJunc.insert(conn.connectingRoad);
        }
        for (XodrId rid : roadsAtJunc) {
            auto it = roadIdToIdx.find(rid);
            if (it != roadIdToIdx.end())
                maxHW = std::max(maxHW, roadHalfWidth(net.roads[it->second]));
        }
        double trimDist = maxHW;

        // Assign trim distances to roads at this junction
        for (const auto& road : net.roads) {
            if (road.predecessor && road.predecessor->elementType == XodrRoadLink::ElementType::Junction
                && road.predecessor->elementId == junc.id) {
                roadTrims[road.id].startTrim = std::max(roadTrims[road.id].startTrim, trimDist);
            }
            if (road.successor && road.successor->elementType == XodrRoadLink::ElementType::Junction
                && road.successor->elementId == junc.id) {
                roadTrims[road.id].endTrim = std::max(roadTrims[road.id].endTrim, trimDist);
            }
        }
    }

    // 4d: Cap trims so we don't over-trim short roads, then store pre-trim endpoints
    struct TrimmedEP {
        GeomUtils::PosHdg startEP; // position after trimming from start
        GeomUtils::PosHdg endEP;   // position after trimming from end
    };
    std::unordered_map<XodrId, TrimmedEP> trimmedEPs;

    for (auto& [rid, ti] : roadTrims) {
        auto it = roadIdToIdx.find(rid);
        if (it == roadIdToIdx.end()) continue;
        const auto& road = net.roads[it->second];
        double maxTotalTrim = road.length * 0.4;
        double total = ti.startTrim + ti.endTrim;
        if (total > maxTotalTrim && total > 0) {
            double scale = maxTotalTrim / total;
            ti.startTrim *= scale;
            ti.endTrim   *= scale;
        }
        // Don't trim less than 0.5m to avoid degenerate connector roads
        if (ti.startTrim > 0 && ti.startTrim < 0.5) ti.startTrim = 0.5;
        if (ti.endTrim > 0 && ti.endTrim < 0.5) ti.endTrim = 0.5;

        TrimmedEP tep;
        if (ti.startTrim > 0)
            tep.startEP = GeomUtils::evaluateChain(road.geometries, ti.startTrim);
        if (ti.endTrim > 0)
            tep.endEP = GeomUtils::evaluateChain(road.geometries, road.length - ti.endTrim);
        trimmedEPs[rid] = tep;
    }

    // 4e: Apply trimming to road geometries
    for (auto& [rid, ti] : roadTrims) {
        auto it = roadIdToIdx.find(rid);
        if (it == roadIdToIdx.end()) continue;
        auto& road = net.roads[it->second];
        // Trim end first (so s-offsets for start trim are still valid)
        if (ti.endTrim > 0)
            GeomUtils::trimChainEnd(road.geometries, ti.endTrim);
        if (ti.startTrim > 0)
            GeomUtils::trimChainStart(road.geometries, ti.startTrim);
        road.length = GeomUtils::chainLength(road.geometries);
    }

    // 4f: Create connector roads for each junction connection
    XodrId nextConnRoadId = nextRoadId; // continue from where road IDs left off

    for (auto& junc : net.junctions) {
        for (auto& conn : junc.connections) {
            auto inIt  = roadIdToIdx.find(conn.incomingRoad);
            auto outIt = roadIdToIdx.find(conn.connectingRoad);
            if (inIt == roadIdToIdx.end() || outIt == roadIdToIdx.end()) continue;

            const auto& inRoad  = net.roads[inIt->second];
            const auto& outRoad = net.roads[outIt->second];
            XodrId origOutRoadId = conn.connectingRoad;

            // Incoming road ends at this junction → use trimmed end position
            auto tepInIt = trimmedEPs.find(conn.incomingRoad);
            GeomUtils::PosHdg connStart;
            if (tepInIt != trimmedEPs.end() && roadTrims[conn.incomingRoad].endTrim > 0)
                connStart = tepInIt->second.endEP;
            else
                connStart = GeomUtils::evaluateChain(inRoad.geometries, inRoad.length);

            // Outgoing road: position depends on contact point
            auto tepOutIt = trimmedEPs.find(origOutRoadId);
            GeomUtils::PosHdg connEnd;
            if (conn.contactPoint == XodrJunctionConnection::ContactPoint::Start) {
                // Outgoing road starts at junction → use trimmed start
                if (tepOutIt != trimmedEPs.end() && roadTrims[origOutRoadId].startTrim > 0)
                    connEnd = tepOutIt->second.startEP;
                else
                    connEnd = GeomUtils::evaluateChain(outRoad.geometries, 0);
            } else {
                // Outgoing road ends at junction → use trimmed end
                if (tepOutIt != trimmedEPs.end() && roadTrims[origOutRoadId].endTrim > 0)
                    connEnd = tepOutIt->second.endEP;
                else
                    connEnd = GeomUtils::evaluateChain(outRoad.geometries, outRoad.length);
            }

            // Build connector road geometry (straight line)
            double dx = connEnd.x - connStart.x;
            double dy = connEnd.y - connStart.y;
            double connLen = std::hypot(dx, dy);
            if (connLen < 0.01) connLen = 0.01;

            XodrGeometry connGeom;
            connGeom.s      = 0.0;
            connGeom.x      = connStart.x;
            connGeom.y      = connStart.y;
            connGeom.hdg    = std::atan2(dy, dx);
            connGeom.length = connLen;

            // Build connector lane section (copy from outgoing road)
            XodrLaneSection connLS;
            connLS.s = 0.0;
            connLS.centerLane.id   = 0;
            connLS.centerLane.type = "none";
            if (!outRoad.laneSections.empty()) {
                connLS = outRoad.laneSections[0];
                connLS.s = 0.0;
                // Update lane widths to use connector length proportionally
                // (keep same width values, they're independent of road length)
            }

            // Create the connector road
            XodrRoad connRoad;
            connRoad.id         = nextConnRoadId++;
            connRoad.name       = "jconn_" + std::to_string(junc.id) + "_" + std::to_string(conn.connectionId);
            connRoad.length     = connLen;
            connRoad.junctionId = junc.id;
            connRoad.geometries.push_back(connGeom);
            connRoad.laneSections.push_back(connLS);

            // Connector predecessor = incoming road (contact at its end)
            {
                XodrRoadLink predLink;
                predLink.elementType  = XodrRoadLink::ElementType::Road;
                predLink.elementId    = conn.incomingRoad;
                predLink.contactPoint = XodrRoadLink::ContactPoint::End;
                connRoad.predecessor  = predLink;
            }
            // Connector successor = outgoing road
            {
                XodrRoadLink succLink;
                succLink.elementType  = XodrRoadLink::ElementType::Road;
                succLink.elementId    = origOutRoadId;
                succLink.contactPoint = conn.contactPoint == XodrJunctionConnection::ContactPoint::Start
                                        ? XodrRoadLink::ContactPoint::Start
                                        : XodrRoadLink::ContactPoint::End;
                connRoad.successor    = succLink;
            }

            // Road type
            {
                XodrRoadTypeRecord rt;
                rt.s        = 0.0;
                rt.type     = "town";
                rt.maxSpeed = 50.0;
                if (!outRoad.roadTypes.empty()) {
                    rt.type     = outRoad.roadTypes[0].type;
                    rt.maxSpeed = outRoad.roadTypes[0].maxSpeed;
                }
                connRoad.roadTypes.push_back(rt);
            }

            net.roads.push_back(std::move(connRoad));

            // Update the junction connection to point to the new connector road
            conn.connectingRoad = net.roads.back().id;
            conn.contactPoint   = XodrJunctionConnection::ContactPoint::Start;
        }
    }

    return net;
}
