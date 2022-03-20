
add_executable(swarmkv-cli swarmkv_cli.c linenoise/linenoise.c)
target_link_libraries(swarmkv-cli swarmkv-shared)
