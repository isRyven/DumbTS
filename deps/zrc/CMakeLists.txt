cmake_minimum_required(VERSION 3.12)
project(cmake-zrc VERSION 0.0.1 LANGUAGES C)

include(CMakeZRC.cmake)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS AND (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR))
    enable_testing()
    if(NOT MSVC)
        add_compile_options(-Wall -Wextra)
    endif()
    add_subdirectory(tests)
endif()
