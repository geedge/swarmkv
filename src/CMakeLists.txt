set(SWARMKV_MAJOR_VERSION 2)
set(SWARMKV_MINOR_VERSION 0)
set(SWARMKV_PATCH_VERSION 0)
set(SWARMKV_VERSION ${SWARMKV_MAJOR_VERSION}.${SWARMKV_MINOR_VERSION}.${SWARMKV_PATCH_VERSION})

message(STATUS "SwarmKV, Version: ${SWARMKV_VERSION}")

add_definitions(-D_GNU_SOURCE) 
add_definitions(-fPIC)
set(SWARMKV_SRC swarmkv.c swarmkv_store.c swarmkv_api.c t_string.c t_cache.c expire.c swarmkv_rr.c
		swarmkv_common.c swarmkv_message.c swarmkv_net.c swarmkv_keyspace.c swarmkv_utils.c future_promise.cpp)
set(LIB_SOURCE_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/../deps/mpack/mpack.c
	${CMAKE_CURRENT_SOURCE_DIR}/../deps/sds/sds.c
	${CMAKE_CURRENT_SOURCE_DIR}/../deps/cJSON/cJSON.c
	${CMAKE_CURRENT_SOURCE_DIR}/../deps/base64/base64.c
	${CMAKE_CURRENT_SOURCE_DIR}/../deps/xxhash/xxhash.c
	${CMAKE_CURRENT_SOURCE_DIR}/../deps/timeout/timeout.c)
	
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/base64/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/mpack/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/sds/
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/uthash
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/xxhash
					${CMAKE_CURRENT_SOURCE_DIR}/../deps/timeout)


# Static Library Output
add_library(swarmkv-static STATIC ${SWARMKV_SRC} ${LIB_SOURCE_FILES})
set_target_properties(swarmkv-static PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmkv-static PROPERTIES OUTPUT_NAME swarmkv)
set_target_properties(swarmkv-static PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(swarmkv-static PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/version.map")
target_include_directories(swarmkv-static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc_internal/)

target_link_libraries(swarmkv-static fieldstat2-static libevent-static)



# Shared Library Output
add_library(swarmkv-shared SHARED ${SWARMKV_SRC} ${LIB_SOURCE_FILES})
set_target_properties(swarmkv-shared PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmkv-shared PROPERTIES OUTPUT_NAME swarmkv)
set_target_properties(swarmkv-shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(swarmkv-shared PROPERTIES VERSION ${SWARMKV_MAJOR_VERSION}.${SWARMKV_MINOR_VERSION})
set_target_properties(swarmkv-shared PROPERTIES SOVERSION ${SWARMKV_MAJOR_VERSION})
set_target_properties(swarmkv-shared PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/version.map")
target_include_directories(swarmkv-shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc_internal/)

target_link_libraries(swarmkv-shared libevent-static libevent-static-pthreads fieldstat2-static ${SWARMKV_DEPEND_DYN_LIB})