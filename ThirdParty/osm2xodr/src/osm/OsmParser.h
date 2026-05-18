#pragma once
#include <string>
#include "OsmTypes.h"

class OsmParser {
public:
    // Parse the OSM XML file and return all raw data.
    // Throws std::runtime_error on I/O or parse failure.
    OsmData parse(const std::string& filePath);
};
