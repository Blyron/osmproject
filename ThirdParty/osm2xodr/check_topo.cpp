#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "src/xodr/XodrTypes.h"

// Minimal XODR parser just for topology checking
#include "pugixml.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) { printf("Usage: check_topo file.xodr\n"); return 1; }

    pugi::xml_document doc;
    if (!doc.load_file(argv[1])) { printf("Failed to load %s\n", argv[1]); return 1; }

    auto root = doc.child("OpenDRIVE");

    // road id -> (start_x, start_y, end_x, end_y)
    struct RoadEP { double sx, sy, ex, ey; };
    std::map<int, RoadEP> roadEps;

    // road id -> predecessor/successor junction id (-1 if road link)
    struct RoadLinks { int predJunc = -1; int succJunc = -1; };
    std::map<int, RoadLinks> roadLinks;

    for (auto road : root.children("road")) {
        int rid = road.attribute("id").as_int();
        auto planView = road.child("planView");

        pugi::xml_node firstGeom, lastGeom;
        for (auto g : planView.children("geometry")) {
            if (!firstGeom) firstGeom = g;
            lastGeom = g;
        }
        if (!firstGeom) continue;

        double sx = firstGeom.attribute("x").as_double();
        double sy = firstGeom.attribute("y").as_double();

        double lx = lastGeom.attribute("x").as_double();
        double ly = lastGeom.attribute("y").as_double();
        double lhdg = lastGeom.attribute("hdg").as_double();
        double llen = lastGeom.attribute("length").as_double();
        double ex = lx + llen * cos(lhdg);
        double ey = ly + llen * sin(lhdg);

        roadEps[rid] = {sx, sy, ex, ey};

        auto link = road.child("link");
        RoadLinks rl;
        auto pred = link.child("predecessor");
        if (pred && std::string(pred.attribute("elementType").as_string()) == "junction")
            rl.predJunc = pred.attribute("elementId").as_int();
        auto succ = link.child("successor");
        if (succ && std::string(succ.attribute("elementType").as_string()) == "junction")
            rl.succJunc = succ.attribute("elementId").as_int();
        roadLinks[rid] = rl;
    }

    // Group roads by junction
    std::map<int, std::vector<std::pair<int, bool>>> juncRoads; // junc_id -> [(road_id, isStart)]
    for (auto& [rid, rl] : roadLinks) {
        if (rl.predJunc >= 0) juncRoads[rl.predJunc].push_back({rid, true});  // road starts at junc (pred=junc)
        if (rl.succJunc >= 0) juncRoads[rl.succJunc].push_back({rid, false}); // road ends at junc (succ=junc)
    }

    int gapCount = 0;
    for (auto& [jid, roads] : juncRoads) {
        if (roads.size() < 2) continue;

        // Get the junction-facing endpoint of each road
        struct PT { int rid; bool isStart; double x, y; };
        std::vector<PT> pts;
        for (auto& [rid, isStart] : roads) {
            auto it = roadEps.find(rid);
            if (it == roadEps.end()) continue;
            if (isStart)
                pts.push_back({rid, true, it->second.sx, it->second.sy});
            else
                pts.push_back({rid, false, it->second.ex, it->second.ey});
        }

        double maxDist = 0;
        int r1 = -1, r2 = -1;
        for (int i = 0; i < (int)pts.size(); i++) {
            for (int j = i+1; j < (int)pts.size(); j++) {
                double d = hypot(pts[i].x - pts[j].x, pts[i].y - pts[j].y);
                if (d > maxDist) {
                    maxDist = d;
                    r1 = i; r2 = j;
                }
            }
        }
        if (maxDist > 0.01) {
            printf("Junction %d: gap=%.4fm  Road %d(%s) (%.3f,%.3f) <-> Road %d(%s) (%.3f,%.3f)\n",
                   jid, maxDist,
                   pts[r1].rid, pts[r1].isStart?"start":"end", pts[r1].x, pts[r1].y,
                   pts[r2].rid, pts[r2].isStart?"start":"end", pts[r2].x, pts[r2].y);
            gapCount++;
        }
    }
    printf("\nTotal junctions with gaps > 0.01m: %d / %d\n", gapCount, (int)juncRoads.size());

    // Also check road-to-road (non-junction) connections
    int roadGaps = 0;
    for (auto road : root.children("road")) {
        int rid = road.attribute("id").as_int();
        auto link = road.child("link");
        if (!link) continue;
        auto it = roadEps.find(rid);
        if (it == roadEps.end()) continue;

        // Check predecessor road link
        auto pred = link.child("predecessor");
        if (pred && std::string(pred.attribute("elementType").as_string()) == "road") {
            int predId = pred.attribute("elementId").as_int();
            std::string cp = pred.attribute("contactPoint").as_string();
            auto pit = roadEps.find(predId);
            if (pit != roadEps.end()) {
                double px, py;
                if (cp == "start") { px = pit->second.sx; py = pit->second.sy; }
                else { px = pit->second.ex; py = pit->second.ey; }
                double d = hypot(it->second.sx - px, it->second.sy - py);
                if (d > 0.01) {
                    printf("Road %d start <-> Road %d %s: gap=%.4fm\n", rid, predId, cp.c_str(), d);
                    roadGaps++;
                }
            }
        }
    }
    printf("Road-to-road gaps > 0.01m: %d\n", roadGaps);
    return 0;
}
