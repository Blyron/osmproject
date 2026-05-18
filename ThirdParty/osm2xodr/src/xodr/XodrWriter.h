#pragma once
#include <string>
#include "XodrTypes.h"

class XodrWriter {
public:
    // Serialize the network to an OpenDRIVE 1.6 XML file.
    // Throws std::runtime_error on write failure.
    void write(const XodrNetwork& net, const std::string& outputPath);
};
