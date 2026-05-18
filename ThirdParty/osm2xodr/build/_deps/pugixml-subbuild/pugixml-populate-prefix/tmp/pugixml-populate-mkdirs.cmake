# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-src")
  file(MAKE_DIRECTORY "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-build"
  "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-subbuild/pugixml-populate-prefix"
  "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-subbuild/pugixml-populate-prefix/tmp"
  "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-subbuild/pugixml-populate-prefix/src/pugixml-populate-stamp"
  "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-subbuild/pugixml-populate-prefix/src"
  "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-subbuild/pugixml-populate-prefix/src/pugixml-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-subbuild/pugixml-populate-prefix/src/pugixml-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/blyro/Documents/Osm2Map/ThirdParty/osm2xodr/build/_deps/pugixml-subbuild/pugixml-populate-prefix/src/pugixml-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
