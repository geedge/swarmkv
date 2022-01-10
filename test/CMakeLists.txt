cmake_minimum_required (VERSION 2.8)

#message("test_dir: ${CMAKE_CURRENT_SOURCE_DIR}")

SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src/inc_internal)

# 创建可执行程序
add_executable(swarmkv_nodeA swarmkv_nodeA.c)
target_link_libraries(swarmkv_nodeA swarmkv_frame_static)


add_executable(swarmkv_nodeB swarmkv_nodeB.c)
target_link_libraries(swarmkv_nodeB swarmkv_frame_static)

add_executable(swarmkv_nodeC swarmkv_nodeC.c)
target_link_libraries(swarmkv_nodeC swarmkv_frame_static)

add_executable(swarmkv_gtest swarmkv_gtest.cpp)
target_link_libraries(swarmkv_gtest swarmkv_frame_static)

add_executable(test_main test_main.c)
target_link_libraries(test_main swarmkv_frame_static)

add_executable(tes_nng tes_nng.c)
target_link_libraries(tes_nng swarmkv_frame_static)

add_executable(multi_thread multi_thread.cpp)
target_link_libraries(multi_thread swarmkv_frame_static)

add_executable(nodeA nodeA_gtest.cpp)
target_link_libraries(nodeA swarmkv_frame_static)

#target_link_libraries(MyTests ${GTEST_BOTH_LIBRARIES})
#target_link_libraries(MyTests ${CMAKE_THREAD_LIBS_INIT})
#add_test(Test MyTests)
#enable_testing()
