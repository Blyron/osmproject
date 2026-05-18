add_executable(check_topo check_topo.cpp)
target_include_directories(check_topo PRIVATE ${pugixml_SOURCE_DIR}/src)
target_link_libraries(check_topo PRIVATE pugixml-static)
