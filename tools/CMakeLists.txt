
include_directories(${PROJECT_SOURCE_DIR}/include/
					${PROJECT_SOURCE_DIR}/src/
					${PROJECT_SOURCE_DIR}/deps/sds/
					${PROJECT_SOURCE_DIR}/deps/log/
					${PROJECT_SOURCE_DIR}/deps/cJSON/
					${PROJECT_SOURCE_DIR}/deps/uthash/
					${PROJECT_SOURCE_DIR}/deps/xxhash)

add_library(swarmkv-tools swarmkv_tools.c)
target_link_libraries(swarmkv-tools swarmkv-static ${SWARMKV_DEPEND_DYN_LIB})

add_executable(swarmkv-cli swarmkv_cli.c linenoise/linenoise.c)
target_link_libraries(swarmkv-cli swarmkv-static swarmkv-tools ${SWARMKV_DEPEND_DYN_LIB})

add_executable(simple_node swarmkv_simple_node.cpp)
target_link_libraries(simple_node swarmkv-static libjemalloc-static dl ${SWARMKV_DEPEND_DYN_LIB})

install(TARGETS swarmkv-cli DESTINATION /usr/local/bin/ COMPONENT TOOLS)