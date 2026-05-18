#include <iostream>
#include <stdexcept>
#include "osm/OsmParser.h"
#include "osm/OsmFilter.h"
#include "conversion/Projection.h"
#include "conversion/RoadNetwork.h"
#include "xodr/XodrWriter.h"

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: osm2xodr <input.osm> <output.xodr>\n";
        return 1;
    }

    const std::string inputFile  = argv[1];
    const std::string outputFile = argv[2];

    try {
        // Stage 1: Parse
        std::cout << "Parsing " << inputFile << " ...\n";
        OsmParser parser;
        OsmData osmData = parser.parse(inputFile);
        std::cout << "  nodes="     << osmData.nodes.size()
                  << "  ways="      << osmData.ways.size()
                  << "  relations=" << osmData.relations.size() << "\n";

        // Stage 2: Filter
        std::cout << "Filtering drivable ways ...\n";
        OsmFilter filter;
        filter.apply(osmData);
        std::cout << "  drivable ways=" << osmData.ways.size() << "\n";

        // Stage 3: Project
        std::cout << "Projecting coordinates ...\n";
        Projection proj(osmData.minLat, osmData.maxLat,
                        osmData.minLon, osmData.maxLon);
        NodeXYMap nodeXY = proj.projectAll(osmData.nodes);

        // Stages 4+5: Build road network
        std::cout << "Building road network ...\n";
        RoadNetwork network;
        XodrNetwork xodrNet = network.build(osmData, nodeXY);
        xodrNet.refLat = proj.refLat();
        xodrNet.refLon = proj.refLon();
        std::cout << "  roads="     << xodrNet.roads.size()
                  << "  junctions=" << xodrNet.junctions.size() << "\n";

        // Stage 6: Write
        std::cout << "Writing " << outputFile << " ...\n";
        XodrWriter writer;
        writer.write(xodrNet, outputFile);
        std::cout << "Done.\n";

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
