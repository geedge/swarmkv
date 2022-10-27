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

# FieldStat2
ExternalProject_Add(FieldStat2 PREFIX fieldstat2
        URL ${CMAKE_CURRENT_SOURCE_DIR}/FieldStat2-v2.10.7.zip
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_C_FLAGS="-fPIC")

ExternalProject_Get_Property(FieldStat2 INSTALL_DIR)
file(MAKE_DIRECTORY ${VENDOR_BUILD}/include)

add_library(fieldstat2-static STATIC IMPORTED GLOBAL)
add_dependencies(fieldstat2-static FieldStat2)
set_property(TARGET fieldstat2-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libMESA_field_stat2.a)
set_property(TARGET fieldstat2-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include)

### Libevent 2.1.12
ExternalProject_Add(libevent PREFIX libevent
        URL ${CMAKE_CURRENT_SOURCE_DIR}/libevent-2.1.12-stable.tar.gz
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${VENDOR_BUILD} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_C_FLAGS="-fPIC")

file(MAKE_DIRECTORY ${VENDOR_BUILD}/include)

add_library(libevent-static STATIC IMPORTED GLOBAL)
add_dependencies(libevent-static libevent)
set_property(TARGET libevent-static PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libevent.a)
set_property(TARGET libevent-static PROPERTY IMPORTED_INTERFACE_LINK_LIBRARIES pthread crypto)
set_property(TARGET libevent-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include)

add_library(libevent-static-pthreads STATIC IMPORTED GLOBAL)
add_dependencies(libevent-static-pthreads libevent)
set_property(TARGET libevent-static-pthreads PROPERTY IMPORTED_LOCATION ${VENDOR_BUILD}/lib/libevent_pthreads.a)
set_property(TARGET libevent-static-pthreads PROPERTY IMPORTED_INTERFACE_LINK_LIBRARIES pthread crypto)
set_property(TARGET libevent-static-pthreads PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VENDOR_BUILD}/include)

## jemalloc
ExternalProject_Add(libjemalloc PREFIX libjemalloc
        URL ${CMAKE_CURRENT_SOURCE_DIR}/jemalloc-5.3.0.tar.bz2
        CONFIGURE_COMMAND ./autogen.sh --prefix=<INSTALL_DIR>
        BUILD_COMMAND make
		BUILD_IN_SOURCE 1
        INSTALL_COMMAND make install prefix=<INSTALL_DIR>)

ExternalProject_Get_Property(libjemalloc INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(libjemalloc-static STATIC IMPORTED GLOBAL)
add_dependencies(libjemalloc-static libjemalloc)
set_property(TARGET libjemalloc-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libjemalloc.a)
set_property(TARGET libjemalloc-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)