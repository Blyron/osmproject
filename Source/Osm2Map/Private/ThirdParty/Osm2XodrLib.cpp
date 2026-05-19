// Unity build wrapper for osm2xodr third-party library.
// Compiles all osm2xodr sources as a single translation unit within UE's build system.

#include "CoreMinimal.h"

// UE defines PI, HALF_PI, TWO_PI, INV_PI, SMALL_NUMBER etc. as macros.
// osm2xodr's Projection.cpp defines its own `static constexpr double PI` which
// conflicts with UE's #define PI. We must undefine them before including third-party code.
#undef PI
#undef HALF_PI
#undef TWO_PI
#undef INV_PI
#undef SMALL_NUMBER
#undef KINDA_SMALL_NUMBER
#undef BIG_NUMBER

// Suppress warnings from third-party code
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4005)  // macro redefinition
#pragma warning(disable: 4244)  // conversion, possible loss of data
#pragma warning(disable: 4267)  // conversion from size_t
#pragma warning(disable: 4706)  // assignment within conditional
#pragma warning(disable: 4100)  // unreferenced formal parameter
#pragma warning(disable: 4458)  // declaration hides class member
#endif

// PugiXML
// OSM parsing
#include "osm/OsmParser.cpp"
#include "osm/OsmFilter.cpp"

// Coordinate projection and geometry
#include "conversion/Projection.cpp"
#include "conversion/GeomUtils.cpp"
#include "conversion/RoadNetwork.cpp"

// OpenDRIVE output
#include "xodr/XodrWriter.cpp"

#ifdef _MSC_VER
#pragma warning(pop)
#endif
