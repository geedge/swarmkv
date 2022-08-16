
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../deps/log/)
add_executable(swarmkv_gtest swarmkv_gtest.cpp test_utils.c ${CMAKE_CURRENT_SOURCE_DIR}/../deps/log/log.c)
add_executable(swarmkv_perf_test swarmkv_perf_test.cpp test_utils.c ${CMAKE_CURRENT_SOURCE_DIR}/../deps/log/log.c)
#include_directories(${PROJECT_SOURCE_DIR}/include)
target_link_libraries(swarmkv_perf_test swarmkv-shared gtest-static)
target_link_libraries(swarmkv_gtest swarmkv-shared gtest-static)
