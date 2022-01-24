set(SWARMKV_MAJOR_VERSION 2)
set(SWARMKV_MINOR_VERSION 0)
set(SWARMKV_PATCH_VERSION 0)
set(SWARMKV_VERSION ${SWARMKV_MAJOR_VERSION}.${SWARMKV_MINOR_VERSION}.${SWARMKV_PATCH_VERSION})

message(STATUS "SwarmKV, Version: ${SWARMKV_VERSION}")

add_definitions(-fPIC)
set(SWARMKV_SRC swarmkv.c mpack.c)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/)


# Static Library Output
add_library(swarmkv_static STATIC ${SWARMKV_SRC})
set_target_properties(swarmkv_static PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmkv_static PROPERTIES OUTPUT_NAME swarmkv)
set_target_properties(swarmkv_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)
target_include_directories(swarmkv_static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc_internal/)

target_link_libraries(swarmkv_static nng-static)
target_link_libraries(swarmkv_static fieldstat2-static)


# Shared Library Output
add_library(swarmkv_shared SHARED ${SWARMKV_SRC})
set_target_properties(swarmkv_shared PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmkv_shared PROPERTIES OUTPUT_NAME swarmkv)
set_target_properties(swarmkv_shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(swarmkv_shared PROPERTIES VERSION ${SWARMKV_MAJOR_VERSION}.${SWARMKV_MINOR_VERSION})
set_target_properties(swarmkv_shared PROPERTIES SOVERSION ${SWARMKV_MAJOR_VERSION})
set_target_properties(swarmkv_shared PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/version.map")
target_include_directories(swarmkv_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc_internal/)

target_link_libraries(swarmkv_shared nng-static)
target_link_libraries(swarmkv_shared fieldstat2-static)

