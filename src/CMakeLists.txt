set(SWARMKV_MAJOR_VERSION 3)
set(SWARMKV_MINOR_VERSION 0)
set(SWARMKV_PATCH_VERSION 0)
set(SWARMKV_VERSION ${SWARMKV_MAJOR_VERSION}.${SWARMKV_MINOR_VERSION}.${SWARMKV_PATCH_VERSION})

message(STATUS "SwarmKV, Version: ${SWARMKV_VERSION}")

add_definitions(-D_GNU_SOURCE) 
add_definitions(-fPIC)
set(SWARMKV_SRC swarmkv.c swarmkv_store.c swarmkv_sync.c swarmkv_api.c
		t_string.c t_set.c t_token_bucket.c t_hash.c
		swarmkv_common.c swarmkv_message.c swarmkv_net.c swarmkv_keyspace.c swarmkv_utils.c 
		swarmkv_monitor.c future_promise.cpp)

set(LIB_SOURCE_FILES
	${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c
	${PROJECT_SOURCE_DIR}/deps/sds/sds.c
	${PROJECT_SOURCE_DIR}/deps/cJSON/cJSON.c
	${PROJECT_SOURCE_DIR}/deps/base64/base64.c
	${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c
	${PROJECT_SOURCE_DIR}/deps/timeout/timeout.c
	${PROJECT_SOURCE_DIR}/deps/log/log.c)
	
include_directories(${PROJECT_SOURCE_DIR}/include/
					${PROJECT_SOURCE_DIR}/CRDT/
					${PROJECT_SOURCE_DIR}/deps/base64/
					${PROJECT_SOURCE_DIR}/deps/mpack/
					${PROJECT_SOURCE_DIR}/deps/sds/
					${PROJECT_SOURCE_DIR}/deps/uthash
					${PROJECT_SOURCE_DIR}/deps/xxhash
					${PROJECT_SOURCE_DIR}/deps/timeout
					${PROJECT_SOURCE_DIR}/deps/cJSON
					${PROJECT_SOURCE_DIR}/deps/log)


# Static Library Output
add_library(swarmkv-static STATIC ${SWARMKV_SRC} ${LIB_SOURCE_FILES})
set_target_properties(swarmkv-static PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmkv-static PROPERTIES OUTPUT_NAME swarmkv)
set_target_properties(swarmkv-static PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(swarmkv-static PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/version.map")
target_include_directories(swarmkv-static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc_internal/)

target_link_libraries(swarmkv-static CRDT libevent-static libevent-static-pthreads libevent-static libhdr-static)



# Shared Library Output
add_library(swarmkv-shared SHARED ${SWARMKV_SRC} ${LIB_SOURCE_FILES} ${CRDT})
set_target_properties(swarmkv-shared PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmkv-shared PROPERTIES OUTPUT_NAME swarmkv)
set_target_properties(swarmkv-shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(swarmkv-shared PROPERTIES VERSION ${SWARMKV_MAJOR_VERSION}.${SWARMKV_MINOR_VERSION})
set_target_properties(swarmkv-shared PROPERTIES SOVERSION ${SWARMKV_MAJOR_VERSION})
set_target_properties(swarmkv-shared PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/version.map")
target_include_directories(swarmkv-shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc_internal/)

target_link_libraries(swarmkv-shared CRDT libevent-static libevent-static-pthreads libhdr-static ${SWARMKV_DEPEND_DYN_LIB})

install(FILES ${PROJECT_SOURCE_DIR}/include/swarmkv/swarmkv.h DESTINATION /opt/MESA/include/MESA/ COMPONENT HEADER)
install(TARGETS swarmkv-shared LIBRARY DESTINATION /opt/MESA/lib/ COMPONENT library)