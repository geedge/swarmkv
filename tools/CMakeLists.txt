
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/
					${CMAKE_CURRENT_SOURCE_DIR}/../src/inc_internal
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/sds/)
add_executable(swarmkv-cli swarmkv_cli.c  linenoise/linenoise.c)
target_link_libraries(swarmkv-cli swarmkv-shared)
