cmake_minimum_required (VERSION 2.8)

project(swarmkv_lib)


#add_library(<name> [STATIC | SHARED | MODULE] [EXCLUDE_FROM_ALL] [source1] [source2] [...])

#set(${SWARMKV_SRC} swarmkv.c swarmkv_net.c swarmkv_message.c)
aux_source_directory(. SWARMKV_SRC)
message("***swarmkv_src: ${SWARMKV_SRC}")
message("***swarmkv_src: ${PROJECT_SOURCE_DIR}")  #/home/janessa/Desktop/swarmkv_cppcheck/src
#include_directories(${PROJECT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc_internal)

add_library(swarmkv_frame_static STATIC ${SWARMKV_SRC})
set_target_properties(swarmkv_frame_static PROPERTIES LINKER_LANGUAGE C)
set_target_properties(swarmkv_frame_static PROPERTIES OUTPUT_NAME swarmkv)

target_link_libraries(swarmkv_frame_static cjson-static)
target_link_libraries(swarmkv_frame_static uuid-static)
target_link_libraries(swarmkv_frame_static gtest) 
target_link_libraries(swarmkv_frame_static nanomsg-static) 
#target_link_libraries(swarmkv_frame_static nng-static) 
#link_directories(/usr/local/lib)
target_link_libraries(swarmkv_frame_static pthread)
target_link_libraries(swarmkv_frame_static crypto)





