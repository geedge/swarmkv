
include_directories(${PROJECT_SOURCE_DIR}/include/
					${PROJECT_SOURCE_DIR}/src/inc_internal
					${PROJECT_SOURCE_DIR}/deps/sds/
					${PROJECT_SOURCE_DIR}/deps/log/
					${PROJECT_SOURCE_DIR}/deps/cJSON/
					${PROJECT_SOURCE_DIR}/deps/uthash/)
add_executable(swarmkv-cli swarmkv_cli.c linenoise/linenoise.c)
target_link_libraries(swarmkv-cli swarmkv-static ${SWARMKV_DEPEND_DYN_LIB})

add_executable(simple_node swarmkv_simple_node.cpp )
target_link_libraries(simple_node swarmkv-static libjemalloc-static dl ${SWARMKV_DEPEND_DYN_LIB})