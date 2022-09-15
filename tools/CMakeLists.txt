
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/
					${CMAKE_CURRENT_SOURCE_DIR}/../src/inc_internal
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/sds/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/log/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/uthash/)
add_executable(swarmkv-cli swarmkv_cli.c  linenoise/linenoise.c)
target_link_libraries(swarmkv-cli swarmkv-shared)
