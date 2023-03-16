add_definitions(-D_GNU_SOURCE) 
add_definitions(-fPIC)

add_library(CRDT lww_register.c pn_counter.c or_map.c or_set.c oc_token_bucket.c ftb_throttler.c)

include_directories(${PROJECT_SOURCE_DIR}/deps/mpack
					${PROJECT_SOURCE_DIR}/deps/uthash
					${PROJECT_SOURCE_DIR}/deps/alias
					${PROJECT_SOURCE_DIR}/deps/xxhash)

add_executable(CRDT_gtest crdt_gtest.cpp ${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c)
target_link_libraries(CRDT_gtest CRDT gtest-static uuid)

add_executable(FTBT_gtest ftbt_gtest.cpp ${PROJECT_SOURCE_DIR}/deps/mpack/mpack.c 
				${PROJECT_SOURCE_DIR}/deps/xxhash/xxhash.c
				${PROJECT_SOURCE_DIR}/deps/alias/ransampl.c
				${PROJECT_SOURCE_DIR}/CRDT/util/fair_index.cpp
				${PROJECT_SOURCE_DIR}/CRDT/util/flow_tokens.cpp)
target_link_libraries(FTBT_gtest CRDT gtest-static uuid)