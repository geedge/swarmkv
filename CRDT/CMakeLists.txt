add_definitions(-D_GNU_SOURCE) 
add_definitions(-fPIC)

add_library(CRDT lww_register.c pn_counter.c or_map.c or_set.c hll_common.c st_hyperloglog.c ap_bloom.c cm_sketch.c
			g_array.c token_bucket_common.c oc_token_bucket.c fair_token_bucket.c bulk_token_bucket.c
			crdt_utils.c)

include_directories(${PROJECT_SOURCE_DIR}/deps/mpack
					${PROJECT_SOURCE_DIR}/deps/uthash
					${PROJECT_SOURCE_DIR}/deps/xxhash)

add_executable(basic_CRDT_gtest basic_crdt_gtest.cpp 
				${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c)
target_link_libraries(basic_CRDT_gtest CRDT gtest-static uuid)

add_executable(tb_CRDT_gtest tb_crdt_gtest.cpp 
				${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c)
target_link_libraries(tb_CRDT_gtest CRDT gtest-static uuid)

add_executable(probabilistic_CRDT_gtest probabilistic_crdt_gtest.cpp
				${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c)
target_link_libraries(probabilistic_CRDT_gtest CRDT gtest-static uuid)

add_executable(ap_bloom_gtest ap_bloom_gtest.cpp
				${PROJECT_SOURCE_DIR}/CRDT/ap_bloom.c
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c
				${PROJECT_SOURCE_DIR}/CRDT/crdt_utils.c)
target_link_libraries(ap_bloom_gtest gtest-static pthread libfieldstat4-static z)	
