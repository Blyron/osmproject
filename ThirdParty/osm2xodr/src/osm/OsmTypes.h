#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

using OsmId  = int64_t;
using TagMap = std::unordered_map<std::string, std::string>;

struct OsmNode {
    OsmId  id  = 0;
    double lat = 0.0;
    double lon = 0.0;
    TagMap tags;
};

struct OsmWay {
    OsmId              id = 0;
    std::vector<OsmId> nodeRefs;
    TagMap             tags;

    std::string highwayType() const {
        auto it = tags.find("highway");
        return (it != tags.end()) ? it->second : "";
    }

    bool isOneway() const {
        auto it = tags.find("oneway");
        if (it != tags.end()) {
            if (it->second == "yes" || it->second == "1") return true;
        }
        auto jt = tags.find("junction");
        if (jt != tags.end() &&
            (jt->second == "roundabout" || jt->second == "circular")) return true;
        return false;
    }

    bool isReversed() const {
        auto it = tags.find("oneway");
        return (it != tags.end() && it->second == "-1");
    }

    bool isRoundabout() const {
        auto it = tags.find("junction");
        return (it != tags.end() &&
                (it->second == "roundabout" || it->second == "circular"));
    }

    int laneCount() const {
        auto it = tags.find("lanes");
        if (it != tags.end()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return 0; // 0 = use default
    }
};

struct OsmRelation {
    OsmId  id = 0;
    TagMap tags;

    struct Member {
        std::string type; // "node","way","relation"
        OsmId       ref  = 0;
        std::string role; // "from","via","to"
    };
    std::vector<Member> members;
};

struct OsmData {
    std::unordered_map<OsmId, OsmNode>     nodes;
    std::unordered_map<OsmId, OsmWay>      ways;
    std::unordered_map<OsmId, OsmRelation> relations;
    double minLat = 0.0, maxLat = 0.0;
    double minLon = 0.0, maxLon = 0.0;
};
