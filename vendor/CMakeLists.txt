cmake_minimum_required (VERSION 2.8)

#[[
MESSAGE("PROJECT_NAME: ${PROJECT_NAME}")       #swarmkv
MESSAGE("PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv
MESSAGE("PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")   #/home/janessa/Desktop/swarmkv/build
MESSAGE("CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv/vendor
]]


include(ExternalProject)

#[[
ExternalProject_Add(GoogleTest PREFIX GoogleTest
        URL ${CMAKE_CURRENT_SOURCE_DIR}/googletest-release-1.8.0.tar.gz
        URL_MD5 16877098823401d1bf2ed7891d7dce36
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(GoogleTest INSTALL_DIR)
message("INSTALL_DIR: ${INSTALL_DIR}")
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(gtest STATIC IMPORTED GLOBAL)
set_property(TARGET gtest PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libgtest.a)
set_property(TARGET gtest PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
set_property(TARGET gtest PROPERTY INTERFACE_LINK_LIBRARIES pthread)
]]

ExternalProject_Add(cJSON PREFIX cJSON
        URL ${CMAKE_CURRENT_SOURCE_DIR}/cJSON-master.zip
        URL_MD5 19a4bd52cd9f3e91c652cabcb1f09e12
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make
        INSTALL_COMMAND ""
        BUILD_IN_SOURCE 1)


ExternalProject_Get_Property(cJSON INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)
message("INSTALL_DIR: ${INSTALL_DIR}")

add_library(cjson STATIC IMPORTED GLOBAL)
set_property(TARGET cjson PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/src/cJSON/libcjson.a)
set_property(TARGET cjson PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)





 
