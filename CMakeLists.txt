cmake_minimum_required (VERSION 2.8)

project(swarmkv) 

include_directories(${PROJECT_SOURCE_DIR}/include)

set(CMAKE_C_FLAGS "-std=gnu99") 

find_program(CMAKE_CXX_CPPCHECK NAMES cppcheck)
if (CMAKE_CXX_CPPCHECK)
    list(
        APPEND CMAKE_CXX_CPPCHECK 
            "--enable=warning"
            "--force"
            "--error-exitcode=1"
            "--suppress=unusedFunction"
            "--suppress=missingInclude"
            "--suppress=uselessAssignmentPtrArg"
            "--suppress=unmatchedSuppression"
    )
    set(CMAKE_C_CPPCHECK ${CMAKE_CXX_CPPCHECK})
else()
  message(FATAL_ERROR "Could not find the program cppcheck.")
endif()

#[[
message("PROJECT_NAME: ${PROJECT_NAME}")               #swarmkv
message("PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")   #/home/janessa/Desktop/swarmkv
message("PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")   #/home/janessa/Desktop/swarmkv/build
message("CAMKE_SOURCE_DIR: ${CAMKE_SOURCE_DIR}")
]]

#add_subdirectory(source_dir [binary_dir] [EXCLUDE_FROM_ALL])
add_subdirectory(vendor)
add_subdirectory(src)
add_subdirectory(test)

#LINK_DIRECTORIES(
#    ${PROJECT_BINARY_DIR}/lib
#    /usr/local/lib
#    /use/lib
#    )


