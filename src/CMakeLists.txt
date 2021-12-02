cmake_minimum_required (VERSION 2.8)

project(swarmkv_lib)

#[[
MESSAGE("PROJECT_NAME: ${PROJECT_NAME}")       #swarmkv
MESSAGE("PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv
MESSAGE("PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")   #/home/janessa/Desktop/swarmkv/build
MESSAGE("CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")  #/home/janessa/Desktop/swarmkv/src
]]

#add_library(<name> [STATIC | SHARED | MODULE] [EXCLUDE_FROM_ALL] [source1] [source2] [...])

#set(${SWARMKV_SRC} swarmkv.c swarmkv_net.c swarmkv_message.c)
aux_source_directory(. SWARMKV_SRC)
message("***swarmkv_src: ${SWARMKV_SRC}")
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc_internal)

add_library(swarmkv_frame_static STATIC ${SWARMKV_SRC})
set_target_properties(swarmkv_frame_static PROPERTIES LINKER_LANGUAGE C)
set_target_properties(swarmkv_frame_static PROPERTIES OUTPUT_NAME swarmkv)

target_link_libraries(swarmkv_frame_static cjson-static)
target_link_libraries(swarmkv_frame_static uuid-static)
target_link_libraries(swarmkv_frame_static gtest) 
#link_directories(/usr/local/lib)
target_link_libraries(swarmkv_frame_static pthread)
target_link_libraries(swarmkv_frame_static crypto)

