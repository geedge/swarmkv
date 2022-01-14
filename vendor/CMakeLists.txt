cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

set(VENDOR_ROOT ${CMAKE_BINARY_DIR}/vendor)
set(VENDOR_BUILD ${CMAKE_BINARY_DIR}/vendor/vbuild)

MESSAGE("Vendor build output path: ${VENDOR_BUILD}")

file(MAKE_DIRECTORY ${VENDOR_BUILD}/include)

#cjson
ExternalProject_Add(cJSON PREFIX cJSON
        URL ${CMAKE_CURRENT_SOURCE_DIR}/cJSON-1.7.14.zip
        URL_MD5 5fc6b4bb6134f7c3bfaca9d260f294d7
        CMAKE_ARGS -DBUILD_SHARED_AND_STATIC_LIBS=On -DENABLE_CJSON_TEST=Off -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD}
        INSTALL_DIR ${VENDOR_BUILD})


add_library(cjson-static STATIC IMPORTED GLOBAL)
add_dependencies(cjson-static cJSON)
set_property(TARGET cjson-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib64/libcjson.a)
#set_property(TARGET cjson-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include/cjson/)


#uuid
#--with-pic, PIC:position independent code
ExternalProject_Add(uuid PREFIX uuid
        URL ${CMAKE_CURRENT_SOURCE_DIR}/libuuid-1.0.3.tar.gz
        URL_MD5 d44d866d06286c08ba0846aba1086d68
        CONFIGURE_COMMAND ./configure --prefix=${VENDOR_BUILD} --disable-shared --with-pic
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1)


add_library(uuid-static STATIC IMPORTED GLOBAL)
add_dependencies(uuid-static uuid)
set_property(TARGET uuid-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libuuid.a)
#set_property(TARGET uuid-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include/uuid)


### GoogleTest
ExternalProject_Add(GoogleTest PREFIX GoogleTest
        URL ${CMAKE_CURRENT_SOURCE_DIR}/googletest-release-1.8.0.tar.gz
        URL_MD5 16877098823401d1bf2ed7891d7dce36
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(GoogleTest INSTALL_DIR)

add_library(gtest STATIC IMPORTED GLOBAL)
add_dependencies(gtest GoogleTest)
set_property(TARGET gtest PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libgtest.a)
set_property(TARGET gtest PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include)
set_property(TARGET gtest PROPERTY INTERFACE_LINK_LIBRARIES pthread)


#nanomsg
#nanomsg在运行时需要glibc中的动态链接库
ExternalProject_Add(nanomsg PREFIX nanomsg
        URL ${CMAKE_CURRENT_SOURCE_DIR}/nanomsg.tar.gz
        URL_MD5 067c174050d39186984c94eb444b05d6
        CMAKE_ARGS -DBUILD_SHARED_AND_STATIC_LIBS=On -DENABLE_NANOMSG_TEST=Off -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD})

ExternalProject_Get_Property(nanomsg INSTALL_DIR)
add_library(nanomsg-static STATIC IMPORTED GLOBAL)
add_dependencies(nanomsg-static nanomsg)
set_property(TARGET nanomsg-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib64/libnanomsg.a)
#set_property(TARGET nanomsg-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/src/nanomsg/)


#fieldstat
ExternalProject_Add(FieldStat PREFIX FieldStat
        URL ${CMAKE_CURRENT_SOURCE_DIR}/FieldState.tar.gz
        URL_MD5 228ca1335d131f9f264200dd67d51167
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(FieldStat INSTALL_DIR)
add_library(fieldstate-static STATIC IMPORTED GLOBAL)
add_dependencies(fieldstate-static FieldStat)
set_property(TARGET fieldstate-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libMESA_field_stat2.a)


#rrpc
ExternalProject_Add(RRPC PREFIX rrpc
        GIT_REPOSITORY https://oauth2:glpat-CFSV3ZCnGzYs3z6C6VWD@git.mesalab.cn/t9k/rrpc.git
        GIT_TAG main
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
add_library(rrpc-static STATIC IMPORTED GLOBAL)
add_dependencies(rrpc-static RRPC)
set_property(TARGET rrpc-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/librrpc-lib.a)