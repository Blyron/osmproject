#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "../osm/OsmTypes.h"

using XodrId = int32_t;

// ---- Geometry ---------------------------------------------------------------

struct XodrGeometry {
    double s      = 0.0;
    double x      = 0.0;
    double y      = 0.0;
    double hdg    = 0.0;   // radians, 0=+X(east), CCW positive
    double length = 0.0;
    // Only Line geometry is generated for now
};

// ---- Lane -------------------------------------------------------------------

struct XodrLane {
    int         id   = 0;
    std::string type = "driving"; // "driving","none","sidewalk","border"
    bool        level = false;

    struct Width {
        double sOffset = 0.0;
        double a = 3.5, b = 0.0, c = 0.0, d = 0.0;
    };
    std::vector<Width> widths; // empty for center lane (id==0)
};

struct XodrLaneSection {
    double                s = 0.0;
    std::vector<XodrLane> leftLanes;   // id > 0, ordered 1..N (inner→outer)
    XodrLane              centerLane;  // id = 0
    std::vector<XodrLane> rightLanes;  // id < 0, ordered -1..-N (inner→outer)
};

// ---- Road links -------------------------------------------------------------

struct XodrRoadLink {
    enum class ElementType { Road, Junction } elementType = ElementType::Road;
    XodrId elementId = -1;
    enum class ContactPoint { Start, End, None } contactPoint = ContactPoint::None;
};

// ---- Road type / speed ------------------------------------------------------

struct XodrRoadTypeRecord {
    double      s        = 0.0;
    std::string type     = "town";
    double      maxSpeed = 50.0;  // km/h
};

// ---- Road -------------------------------------------------------------------

struct XodrRoad {
    XodrId      id         = -1;
    std::string name;
    double      length     = 0.0;
    XodrId      junctionId = -1; // -1 = not part of a junction

    std::optional<XodrRoadLink> predecessor;
    std::optional<XodrRoadLink> successor;

    std::vector<XodrGeometry>       geometries;
    std::vector<XodrLaneSection>    laneSections;
    std::vector<XodrRoadTypeRecord> roadTypes;

    // Debug / bookkeeping
    OsmId sourceWayId    = 0;
    OsmId startNodeOsmId = 0;
    OsmId endNodeOsmId   = 0;
};

// ---- Junction ---------------------------------------------------------------

struct XodrJunctionConnection {
    XodrId connectionId   = -1;
    XodrId incomingRoad   = -1;
    XodrId connectingRoad = -1;
    enum class ContactPoint { Start, End } contactPoint = ContactPoint::Start;

    struct LaneLink { int from = 0; int to = 0; };
    std::vector<LaneLink> laneLinks;
};

struct XodrJunction {
    XodrId      id = -1;
    std::string name;
    std::vector<XodrJunctionConnection> connections;
};

// ---- Top-level network ------------------------------------------------------

struct XodrNetwork {
    std::vector<XodrRoad>    roads;
    std::vector<XodrJunction> junctions;
    double refLat = 0.0;
    double refLon = 0.0;
};
