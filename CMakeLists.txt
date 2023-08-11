cmake_minimum_required (VERSION 3.5)

set(lib_name swarmkv)
project(${lib_name})

include_directories(${PROJECT_SOURCE_DIR}/include)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Version)

set(CMAKE_C_FLAGS "-std=gnu99 -Wall") 
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall)
set(SWARMKV_DEPEND_DYN_LIB pthread uuid m)

include_directories(${PROJECT_SOURCE_DIR}/inc/)
include_directories(/opt/MESA/include/)

find_program(CMAKE_CXX_CPPCHECK NAMES cppcheck)
if (CMAKE_CXX_CPPCHECK)
    list(
        APPEND CMAKE_CXX_CPPCHECK 
            "--enable=all"
            "--error-exitcode=1"
            "--suppress=unusedFunction"
            "--suppress=missingInclude"
            "--suppress=uselessAssignmentPtrArg"
            "--suppress=unmatchedSuppression"
            "--suppress=variableScope"
            "--suppress=unreadVariable"
            "--suppress=cstyleCast"
            "--suppress=memleakOnRealloc"
            "--suppress=constParameter"
            "--suppress=uselessAssignmentArg"
            "--suppress=uninitvar"
            "--suppress=unusedStructMember"
            "--suppress=internalAstError"
            "--suppress=redundantAssignment"
            "--suppress=duplicateValueTernary"
            "--suppress=sizeofDivisionMemfunc"
            "--suppress=nullPointerRedundantCheck"
            "--suppress=nullPointerArithmeticRedundantCheck"
            "--suppress=shiftNegative"
            "--suppress=shiftTooManyBits"
            "--suppress=signConversionCond"
            "--suppress=pointerSize"
            "-I/usr/include/"
    )
    set(CMAKE_C_CPPCHECK ${CMAKE_CXX_CPPCHECK})
else()
  message(FATAL_ERROR "Could not find the program cppcheck.")
endif()

include_directories(include)
#add_subdirectory(source_dir [binary_dir] [EXCLUDE_FROM_ALL])
add_subdirectory(vendor)
add_subdirectory(CRDT)
add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(tools)
add_subdirectory(examples)
#LINK_DIRECTORIES(
#    ${PROJECT_BINARY_DIR}/lib
#    /usr/local/lib
#    /use/lib
#    )

include(Package)

