cmake_minimum_required(VERSION 3.22.1)
project(FCT_SAI LANGUAGES CXX)

include("../../Config/copy_headers.cmake")
include("../../Config/set_utf8_compiler_encoding.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE FCT_SAI_HEADERS
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)
list(REMOVE_ITEM FCT_SAI_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/FCT_SAI.h")

file(GLOB_RECURSE FCT_SAI_SOURCES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(FCT_SAI STATIC
    ${FCT_SAI_SOURCES}
    ${FCT_SAI_HEADERS}
)

set_utf8_for_target(FCT_SAI)

if(MSVC)
    target_compile_options(FCT_SAI PRIVATE /EHsc)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/FCT_SAI")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/FCT_SAI.h")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/FCT_SAI.h" "${CMAKE_CURRENT_SOURCE_DIR}/include/FCT_SAI.h" COPYONLY)
endif()

foreach(header_file ${FCT_SAI_HEADERS})
    file(RELATIVE_PATH relative_path "${CMAKE_CURRENT_SOURCE_DIR}/src" "${header_file}")
    get_filename_component(header_dir "${relative_path}" DIRECTORY)
    if(header_dir)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/FCT_SAI/${header_dir}")
    endif()
    configure_file("${header_file}" "${CMAKE_CURRENT_SOURCE_DIR}/include/FCT_SAI/${relative_path}" COPYONLY)
endforeach()

add_copy_target_headers(FCT_SAI "${FCT_SAI_HEADERS}")

target_include_directories(FCT_SAI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

find_package(absl CONFIG REQUIRED)

target_link_libraries(FCT_SAI PUBLIC
    FCT_Base
    absl::status
    absl::statusor
    absl::strings
)

if(WIN32)
    target_link_libraries(FCT_SAI PUBLIC dwrite)
endif()

function(target_link_fct_sai_libraries target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()
    target_link_libraries(${target_name} FCT_SAI)
endfunction()
