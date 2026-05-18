#include "XodrWriter.h"
#include <pugixml.hpp>
#include <stdexcept>
#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip>

// ---- helpers ----------------------------------------------------------------

static std::string isoNow()
{
    std::time_t t = std::time(nullptr);
    char buf[32];
#if defined(_MSC_VER)
    struct tm tm_buf;
    gmtime_s(&tm_buf, &t);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
#else
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));
#endif
    return buf;
}

static std::string fmtD(double v, int prec = 6)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

static std::string contactPointStr(XodrRoadLink::ContactPoint cp)
{
    switch (cp) {
        case XodrRoadLink::ContactPoint::Start: return "start";
        case XodrRoadLink::ContactPoint::End:   return "end";
        default:                                return "";
    }
}

static std::string juncConnCPStr(XodrJunctionConnection::ContactPoint cp)
{
    return (cp == XodrJunctionConnection::ContactPoint::Start) ? "start" : "end";
}

// ---- road serialisation ------------------------------------------------------

static void writeLanes(pugi::xml_node parent, const XodrRoad& road)
{
    pugi::xml_node lanes = parent.append_child("lanes");

    for (const auto& sec : road.laneSections) {
        pugi::xml_node lsNode = lanes.append_child("laneSection");
        lsNode.append_attribute("s") = fmtD(sec.s).c_str();

        // left
        pugi::xml_node leftNode = lsNode.append_child("left");
        for (const auto& lane : sec.leftLanes) {
            pugi::xml_node ln = leftNode.append_child("lane");
            ln.append_attribute("id")    = lane.id;
            ln.append_attribute("type")  = lane.type.c_str();
            ln.append_attribute("level") = lane.level ? "true" : "false";
            for (const auto& w : lane.widths) {
                pugi::xml_node wn = ln.append_child("width");
                wn.append_attribute("sOffset") = fmtD(w.sOffset).c_str();
                wn.append_attribute("a")        = fmtD(w.a).c_str();
                wn.append_attribute("b")        = fmtD(w.b).c_str();
                wn.append_attribute("c")        = fmtD(w.c).c_str();
                wn.append_attribute("d")        = fmtD(w.d).c_str();
            }
        }
        if (sec.leftLanes.empty()) {
            // write empty <left/> for spec compliance
        }

        // center
        pugi::xml_node centerNode = lsNode.append_child("center");
        pugi::xml_node cln = centerNode.append_child("lane");
        cln.append_attribute("id")    = sec.centerLane.id;
        cln.append_attribute("type")  = sec.centerLane.type.c_str();
        cln.append_attribute("level") = sec.centerLane.level ? "true" : "false";

        // right
        pugi::xml_node rightNode = lsNode.append_child("right");
        for (const auto& lane : sec.rightLanes) {
            pugi::xml_node ln = rightNode.append_child("lane");
            ln.append_attribute("id")    = lane.id;
            ln.append_attribute("type")  = lane.type.c_str();
            ln.append_attribute("level") = lane.level ? "true" : "false";
            for (const auto& w : lane.widths) {
                pugi::xml_node wn = ln.append_child("width");
                wn.append_attribute("sOffset") = fmtD(w.sOffset).c_str();
                wn.append_attribute("a")        = fmtD(w.a).c_str();
                wn.append_attribute("b")        = fmtD(w.b).c_str();
                wn.append_attribute("c")        = fmtD(w.c).c_str();
                wn.append_attribute("d")        = fmtD(w.d).c_str();
            }
        }
    }
}

static void writeRoad(pugi::xml_node parent, const XodrRoad& road)
{
    pugi::xml_node r = parent.append_child("road");
    r.append_attribute("name")     = road.name.c_str();
    r.append_attribute("length")   = fmtD(road.length).c_str();
    r.append_attribute("id")       = road.id;
    r.append_attribute("junction") = road.junctionId;

    // link
    if (road.predecessor.has_value() || road.successor.has_value()) {
        pugi::xml_node link = r.append_child("link");
        if (road.predecessor.has_value()) {
            const auto& pred = *road.predecessor;
            pugi::xml_node pn = link.append_child("predecessor");
            pn.append_attribute("elementType") =
                (pred.elementType == XodrRoadLink::ElementType::Junction)
                    ? "junction" : "road";
            pn.append_attribute("elementId") = pred.elementId;
            std::string cp = contactPointStr(pred.contactPoint);
            if (!cp.empty()) pn.append_attribute("contactPoint") = cp.c_str();
        }
        if (road.successor.has_value()) {
            const auto& succ = *road.successor;
            pugi::xml_node sn = link.append_child("successor");
            sn.append_attribute("elementType") =
                (succ.elementType == XodrRoadLink::ElementType::Junction)
                    ? "junction" : "road";
            sn.append_attribute("elementId") = succ.elementId;
            std::string cp = contactPointStr(succ.contactPoint);
            if (!cp.empty()) sn.append_attribute("contactPoint") = cp.c_str();
        }
    }

    // type
    for (const auto& rt : road.roadTypes) {
        pugi::xml_node typeNode = r.append_child("type");
        typeNode.append_attribute("s")    = fmtD(rt.s).c_str();
        typeNode.append_attribute("type") = rt.type.c_str();
        pugi::xml_node speed = typeNode.append_child("speed");
        speed.append_attribute("max")  = fmtD(rt.maxSpeed, 1).c_str();
        speed.append_attribute("unit") = "km/h";
    }

    // planView
    pugi::xml_node pv = r.append_child("planView");
    for (const auto& g : road.geometries) {
        pugi::xml_node gn = pv.append_child("geometry");
        gn.append_attribute("s")      = fmtD(g.s).c_str();
        gn.append_attribute("x")      = fmtD(g.x).c_str();
        gn.append_attribute("y")      = fmtD(g.y).c_str();
        gn.append_attribute("hdg")    = fmtD(g.hdg).c_str();
        gn.append_attribute("length") = fmtD(g.length).c_str();
        gn.append_child("line"); // always line geometry
    }

    // elevationProfile (flat, all zeros)
    pugi::xml_node ep = r.append_child("elevationProfile");
    pugi::xml_node elev = ep.append_child("elevation");
    elev.append_attribute("s") = "0.0";
    elev.append_attribute("a") = "0.0";
    elev.append_attribute("b") = "0.0";
    elev.append_attribute("c") = "0.0";
    elev.append_attribute("d") = "0.0";

    // lateralProfile (empty)
    r.append_child("lateralProfile");

    // lanes
    writeLanes(r, road);

    // objects & signals (empty)
    r.append_child("objects");
    r.append_child("signals");
}

// ---- junction serialisation -------------------------------------------------

static void writeJunction(pugi::xml_node parent, const XodrJunction& junc)
{
    pugi::xml_node jn = parent.append_child("junction");
    jn.append_attribute("name") = junc.name.c_str();
    jn.append_attribute("id")   = junc.id;

    for (const auto& conn : junc.connections) {
        pugi::xml_node cn = jn.append_child("connection");
        cn.append_attribute("id")           = conn.connectionId;
        cn.append_attribute("incomingRoad") = conn.incomingRoad;
        cn.append_attribute("connectingRoad") = conn.connectingRoad;
        cn.append_attribute("contactPoint") = juncConnCPStr(conn.contactPoint).c_str();
        for (const auto& ll : conn.laneLinks) {
            pugi::xml_node lln = cn.append_child("laneLink");
            lln.append_attribute("from") = ll.from;
            lln.append_attribute("to")   = ll.to;
        }
    }
}

// ---- public API -------------------------------------------------------------

void XodrWriter::write(const XodrNetwork& net, const std::string& outputPath)
{
    pugi::xml_document doc;

    // XML declaration
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version")  = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    // Root element
    pugi::xml_node root = doc.append_child("OpenDRIVE");

    // Header
    pugi::xml_node header = root.append_child("header");
    header.append_attribute("revMajor") = "1";
    header.append_attribute("revMinor") = "6";
    header.append_attribute("name")     = "osm2xodr";
    header.append_attribute("version")  = "1.00";
    header.append_attribute("date")     = isoNow().c_str();
    header.append_attribute("vendor")   = "osm2xodr";

    // Roads (non-junction first, then junction-member roads)
    for (const auto& road : net.roads) {
        if (road.junctionId == -1) writeRoad(root, road);
    }
    for (const auto& road : net.roads) {
        if (road.junctionId != -1) writeRoad(root, road);
    }

    // Junctions
    for (const auto& junc : net.junctions) {
        writeJunction(root, junc);
    }

    if (!doc.save_file(outputPath.c_str(), "  ")) {
        throw std::runtime_error("Failed to write XODR file: " + outputPath);
    }
}
