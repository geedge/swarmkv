cmake_minimum_required (VERSION 2.8)

project(swarmkv)   #set project_name

include_directories(${PROJECT_SOURCE_DIR}/include)

#[[
message("PROJECT_NAME: ${PROJECT_NAME}")               #swarmkv
message("PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv
message("PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")   #/home/janessa/Desktop/swarmkv/build
message("CAMKE_SOURCE_DIR: ${CAMKE_SOURCE_DIR}")
]]

#add_subdirectory(source_dir [binary_dir] [EXCLUDE_FROM_ALL])
#add_subdirectory(vendor)
add_subdirectory(src)
add_subdirectory(test)

#LINK_DIRECTORIES(
#    ${PROJECT_BINARY_DIR}/lib
#    /usr/local/lib
#    /use/lib
#    )

#SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
#SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

#ADD_SUBDIRECTORY(vendor)
#ADD_SUBDIRECTORY(src)

#INCLUDE_DIRECTORIES(include)
#AUX_SOURCE_DIRECTORY(src DIR_SRCS)     #把src目录下的所有源文件存到变量DIR_SRCS中

#add_executable(swarmkv ${DIR_SRCS})   #生成可执行文件，swarmkv swarmkv.c


#TARGET_LINK_LIBRARIES(swarmkv cJSON-static)
#TARGET_LINK_LIBRARIES(swarmkv /home/janessa/Desktop/swarmkv/vendor/cJSON/src/cJSON/libcjson.a)

#添加动态连接库的路径
#link_directories(/usr/local/lib) 
#target_link_libraries(swarmkv -lcjson ) 
#target_link_libraries(swarmkv -lpthread ) 
#target_link_libraries(swarmkv -luuid )

