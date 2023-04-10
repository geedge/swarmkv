add_definitions(-D_GNU_SOURCE) 
add_definitions(-fPIC)

add_library(CRDT lww_register.c pn_counter.c or_map.c or_set.c oc_token_bucket.c cm_sketch.c st_hll.c
			fs_token_bucket.c)

include_directories(${PROJECT_SOURCE_DIR}/deps/mpack
					${PROJECT_SOURCE_DIR}/deps/uthash
					${PROJECT_SOURCE_DIR}/deps/xxhash)

add_executable(CRDT_gtest crdt_gtest.cpp 
				${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c)
target_link_libraries(CRDT_gtest CRDT gtest-static uuid)