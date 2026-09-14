cmake_minimum_required(VERSION 3.20)

project(ARIEC61850SclAssistedProfile LANGUAGES CXX)

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_WARNINGS_AS_ERRORS ON CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. arstack-core)

add_executable(
    scl_assisted_connect_profile
    ${CMAKE_CURRENT_LIST_DIR}/../test_scl_assisted_connect.cpp)
target_link_libraries(scl_assisted_connect_profile PRIVATE ARIEC61850::core)
target_compile_features(scl_assisted_connect_profile PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(scl_assisted_connect_profile PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(scl_assisted_connect_profile PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
