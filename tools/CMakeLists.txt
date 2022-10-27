
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/
					${CMAKE_CURRENT_SOURCE_DIR}/../src/inc_internal
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/sds/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/log/
					${PROJECT_SOURCE_DIR}/deps/cJSON/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/uthash/)
add_executable(swarmkv-cli swarmkv_cli.c  linenoise/linenoise.c)
target_link_libraries(swarmkv-cli swarmkv-shared)

add_executable(simple_node swarmkv_simple_node.cpp)
target_link_libraries(simple_node swarmkv-shared libjemalloc-static dl)