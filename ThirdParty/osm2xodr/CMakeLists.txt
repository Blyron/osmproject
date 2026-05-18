cmake_minimum_required(VERSION 3.20)
project(osm2xodr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------- Dependencies: PugiXML via FetchContent ---------------------------
include(FetchContent)
FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG        v1.14
)
FetchContent_MakeAvailable(pugixml)

# ---------- Sources ----------------------------------------------------------
set(SOURCES
  src/main.cpp
  src/osm/OsmParser.cpp
  src/osm/OsmFilter.cpp
  src/conversion/Projection.cpp
  src/conversion/GeomUtils.cpp
  src/conversion/RoadNetwork.cpp
  src/xodr/XodrWriter.cpp
)

add_executable(osm2xodr ${SOURCES})

target_include_directories(osm2xodr PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(osm2xodr PRIVATE
  pugixml::pugixml
)

# ---------- Compiler warnings ------------------------------------------------
target_compile_options(osm2xodr PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# ---------- Install ----------------------------------------------------------
install(TARGETS osm2xodr DESTINATION bin)
