#include "OsmParser.h"
#include <stdexcept>
#include <pugixml.hpp>

OsmData OsmParser::parse(const std::string& filePath)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filePath.c_str());
    if (!result) {
        throw std::runtime_error(
            std::string("Failed to parse OSM file: ") + result.description());
    }

    OsmData data;
    pugi::xml_node root = doc.child("osm");

    // Bounds
    if (pugi::xml_node bounds = root.child("bounds")) {
        data.minLat = bounds.attribute("minlat").as_double();
        data.maxLat = bounds.attribute("maxlat").as_double();
        data.minLon = bounds.attribute("minlon").as_double();
        data.maxLon = bounds.attribute("maxlon").as_double();
    }

    // Nodes
    for (pugi::xml_node xn : root.children("node")) {
        OsmNode n;
        n.id  = xn.attribute("id").as_llong();
        n.lat = xn.attribute("lat").as_double();
        n.lon = xn.attribute("lon").as_double();
        for (pugi::xml_node tag : xn.children("tag")) {
            n.tags[tag.attribute("k").as_string()] =
                tag.attribute("v").as_string();
        }
        data.nodes[n.id] = std::move(n);
    }

    // Ways
    for (pugi::xml_node xw : root.children("way")) {
        OsmWay w;
        w.id = xw.attribute("id").as_llong();
        for (pugi::xml_node nd : xw.children("nd")) {
            w.nodeRefs.push_back(nd.attribute("ref").as_llong());
        }
        for (pugi::xml_node tag : xw.children("tag")) {
            w.tags[tag.attribute("k").as_string()] =
                tag.attribute("v").as_string();
        }
        data.ways[w.id] = std::move(w);
    }

    // Relations
    for (pugi::xml_node xr : root.children("relation")) {
        OsmRelation r;
        r.id = xr.attribute("id").as_llong();
        for (pugi::xml_node mem : xr.children("member")) {
            OsmRelation::Member m;
            m.type = mem.attribute("type").as_string();
            m.ref  = mem.attribute("ref").as_llong();
            m.role = mem.attribute("role").as_string();
            r.members.push_back(std::move(m));
        }
        for (pugi::xml_node tag : xr.children("tag")) {
            r.tags[tag.attribute("k").as_string()] =
                tag.attribute("v").as_string();
        }
        data.relations[r.id] = std::move(r);
    }

    return data;
}
