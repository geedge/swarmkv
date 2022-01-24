# CMakeFiles for 3rd vendor library

include(ExternalProject)

set(VENDOR_ROOT ${CMAKE_BINARY_DIR}/vendor)
set(VENDOR_BUILD ${CMAKE_BINARY_DIR}/vendor/vbuild)

# GoogleTest
ExternalProject_Add(GoogleTest PREFIX GoogleTest
        URL ${CMAKE_CURRENT_SOURCE_DIR}/googletest-release-1.8.0.tar.gz
        URL_MD5 16877098823401d1bf2ed7891d7dce36
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(GoogleTest INSTALL_DIR)
file(MAKE_DIRECTORY ${VENDOR_BUILD}/include)

add_library(gtest-static STATIC IMPORTED GLOBAL)
set_property(TARGET gtest-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libgtest.a)
set_property(TARGET gtest-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include)
set_property(TARGET gtest-static PROPERTY INTERFACE_LINK_LIBRARIES pthread)


# Nanomsg NG
ExternalProject_Add(Nanomsg-NG PREFIX nng
        URL ${CMAKE_CURRENT_SOURCE_DIR}/nng-1.5.2.zip
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(Nanomsg-NG INSTALL_DIR)
file(MAKE_DIRECTORY ${VENDOR_BUILD}/include)

add_library(nng-static STATIC IMPORTED GLOBAL)
set_property(TARGET nng-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libnng.a)
set_property(TARGET nng-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include)

# FieldStat2
ExternalProject_Add(FieldStat2 PREFIX fieldstat2
        URL ${CMAKE_CURRENT_SOURCE_DIR}/FieldStat2-v2.10.0.zip
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(FieldStat2 INSTALL_DIR)
file(MAKE_DIRECTORY ${VENDOR_BUILD}/include)

add_library(fieldstat2-static STATIC IMPORTED GLOBAL)
add_dependencies(fieldstat2-static FieldStat2)
set_property(TARGET fieldstat2-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libMESA_field_stat2.a)
set_property(TARGET fieldstat2-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include)
