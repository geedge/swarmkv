cmake_minimum_required (VERSION 2.8)

project(swarmkv) 

include_directories(${PROJECT_SOURCE_DIR}/include)

#[[
message("PROJECT_NAME: ${PROJECT_NAME}")               #swarmkv
message("PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv
message("PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")   #/home/janessa/Desktop/swarmkv/build
message("CAMKE_SOURCE_DIR: ${CAMKE_SOURCE_DIR}")
]]

#add_subdirectory(source_dir [binary_dir] [EXCLUDE_FROM_ALL])
add_subdirectory(vendor)
add_subdirectory(src)
add_subdirectory(test)

#LINK_DIRECTORIES(
#    ${PROJECT_BINARY_DIR}/lib
#    /usr/local/lib
#    /use/lib
#    )


