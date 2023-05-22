add_definitions(-D_GNU_SOURCE) 
add_definitions(-fPIC)

add_library(CRDT lww_register.c pn_counter.c or_map.c or_set.c cm_sketch.c st_hyperloglog.c
			g_array.c token_bucket_common.c oc_token_bucket.c fair_token_bucket.c bulk_token_bucket.c
            heavy_keeper.c sorted_set.c)

include_directories(${PROJECT_SOURCE_DIR}/deps/mpack
					${PROJECT_SOURCE_DIR}/deps/uthash
					${PROJECT_SOURCE_DIR}/deps/xxhash
                    ${PROJECT_SOURCE_DIR}/deps/minheap)

add_executable(CRDT_base_gtest crdt_base_gtest.cpp 
				${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c)
target_link_libraries(CRDT_base_gtest CRDT gtest-static uuid)

add_executable(CRDT_tb_gtest crdt_tb_gtest.cpp 
				${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c)
target_link_libraries(CRDT_tb_gtest CRDT gtest-static uuid)