cmake_minimum_required (VERSION 3.5)

project(swarmkv) 

include_directories(${PROJECT_SOURCE_DIR}/include)

set(CMAKE_C_FLAGS "-std=gnu99 -Wall") 
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall)
set(SWARMKV_DEPEND_DYN_LIB MESA_handle_logger pthread)

include_directories(${PROJECT_SOURCE_DIR}/inc/)
include_directories(/opt/MESA/include/)
# find_program(CMAKE_CXX_CPPCHECK NAMES cppcheck)
# if (CMAKE_CXX_CPPCHECK)
    # list(
        # APPEND CMAKE_CXX_CPPCHECK 
            # "--enable=warning"
            # "--enable=all"
            # "--force"
            # "--std=c99"
            # "--error-exitcode=1"
            # "--suppress=unreadVariable"
            # "--suppress=variableScope"
            # "--suppress=ConfigurationNotChecked"
            # "--suppress=unreachableCode"
            # "--suppress=unusedFunction"
            # "--suppress=missingInclude"
            # "--suppress=uselessAssignmentPtrArg"
            # "--suppress=unmatchedSuppression"
    # )
    # set(CMAKE_C_CPPCHECK ${CMAKE_CXX_CPPCHECK})
# else()
  # message(FATAL_ERROR "Could not find the program cppcheck.")
# endif()

include_directories(include)
#add_subdirectory(source_dir [binary_dir] [EXCLUDE_FROM_ALL])
add_subdirectory(vendor)
add_subdirectory(src)
add_subdirectory(test)

#LINK_DIRECTORIES(
#    ${PROJECT_BINARY_DIR}/lib
#    /usr/local/lib
#    /use/lib
#    )


