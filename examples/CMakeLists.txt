include_directories(${PROJECT_SOURCE_DIR}include/)
add_executable(simple_example simple_example.c)
target_link_libraries(simple_example swarmkv-static pthread uuid)