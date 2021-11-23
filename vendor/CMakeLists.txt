cmake_minimum_required (VERSION 2.8)

include(ExternalProject)

#[[
MESSAGE("PROJECT_NAME: ${PROJECT_NAME}")       #swarmkv
MESSAGE("PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv
MESSAGE("PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")   #/home/janessa/Desktop/swarmkv/build
MESSAGE("CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv/vendor
]]

#caculate md5: md5sum filename
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

#cjson
ExternalProject_Add(cJSON PREFIX cJSON
        URL ${CMAKE_CURRENT_SOURCE_DIR}/cJSON-1.7.14.zip
        URL_MD5 5fc6b4bb6134f7c3bfaca9d260f294d7
        CONFIGURE_COMMAND cmake
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1)

ExternalProject_Get_Property(cJSON INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)
message("INSTALL_DIR: ${INSTALL_DIR}")    #/build/vendor/cJSON

add_library(cjson-static STATIC IMPORTED GLOBAL)
set_property(TARGET cjson-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/src/cJSON/libcjson.a)
set_property(TARGET cjson-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)


#uuid
#--with-pic, PIC:position independent code
ExternalProject_Add(uuid PREFIX uuid
        URL ${CMAKE_CURRENT_SOURCE_DIR}/libuuid-1.0.3.tar.gz
        URL_MD5 d44d866d06286c08ba0846aba1086d68
        CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR> --disable-shared --with-pic
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1)

message("INSTALL_DIR: ${INSTALL_DIR}")

ExternalProject_Get_Property(uuid INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(uuid-static STATIC IMPORTED GLOBAL)
#add_dependencies(uuid-static uuid)
set_property(TARGET uuid-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/src/uuid/.libs/libuuid.a)
set_property(TARGET uuid-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)


### GoogleTest
ExternalProject_Add(GoogleTest PREFIX GoogleTest
        URL ${CMAKE_CURRENT_SOURCE_DIR}/googletest-release-1.8.0.tar.gz
        URL_MD5 16877098823401d1bf2ed7891d7dce36
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(GoogleTest INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(gtest STATIC IMPORTED GLOBAL)
set_property(TARGET gtest PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libgtest.a)
set_property(TARGET gtest PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
set_property(TARGET gtest PROPERTY INTERFACE_LINK_LIBRARIES pthread)

#[[
#openssl-3.0.0
ExternalProject_Add(openssl PREFIX openssl
        URL ${CMAKE_CURRENT_SOURCE_DIR}/openssl-3.0.0.tar.gz
        URL_MD5 43c5ab628b7ab899d7cd4a4c7fe4067f
        CONFIGURE_COMMAND ./Configure --prefix=<INSTALL_DIR> --disable-shared
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1)

message("INSTALL_DIR: ${INSTALL_DIR}")

ExternalProject_Get_Property(openssl INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(openssl-static STATIC IMPORTED GLOBAL)
set_property(TARGET openssl-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/src/openssl/.libs/libopenssl.a)
set_property(TARGET openssl-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
]]

#[[
### libxml2
ExternalProject_Add(libxml2
        PREFIX libxml2
        URL ${CMAKE_CURRENT_SOURCE_DIR}/libxml2-2.9.2.tar.gz
        URL_MD5 9e6a9aca9d155737868b3dc5fd82f788
        CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR> --disable-shared --without-python
        BUILD_IN_SOURCE 1)

ExternalProject_Get_Property(libxml2 INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include/libxml2/)

add_library(libxml2-static STATIC IMPORTED GLOBAL)
add_dependencies(libxml2-static libxml2)
set_property(TARGET libxml2-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libxml2.a)
set_property(TARGET libxml2-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include/libxml2/)
]]







 
