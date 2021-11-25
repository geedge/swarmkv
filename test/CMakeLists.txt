cmake_minimum_required (VERSION 2.8)

#message("test_dir: ${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}../include)

# 创建可执行程序
add_executable(swarmkv_nodeA swarmkv_nodeA.c)
target_link_libraries(swarmkv_nodeA swarmkv_frame_static)


add_executable(swarmkv_nodeB swarmkv_nodeB.c)
target_link_libraries(swarmkv_nodeB swarmkv_frame_static)

add_executable(swarmkv_gtest swarmkv_gtest.cpp)
target_link_libraries(swarmkv_gtest swarmkv_frame_static)

#target_link_libraries(MyTests ${GTEST_BOTH_LIBRARIES})
#target_link_libraries(MyTests ${CMAKE_THREAD_LIBS_INIT})
#add_test(Test MyTests)
#enable_testing()
