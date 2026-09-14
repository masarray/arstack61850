cmake_minimum_required(VERSION 3.20)

project(ARIEC61850SclAssistedProfile LANGUAGES CXX)

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_WARNINGS_AS_ERRORS ON CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. arstack-core)

function(add_scl_profile_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ARIEC61850::core)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endfunction()

add_scl_profile_test(
    scl_assisted_connect_profile
    ${CMAKE_CURRENT_LIST_DIR}/../test_scl_assisted_connect.cpp)
add_scl_profile_test(
    scl_association_addressing_profile
    ${CMAKE_CURRENT_LIST_DIR}/../test_scl_association_addressing.cpp)
add_scl_profile_test(
    scl_sync_health_profile
    ${CMAKE_CURRENT_LIST_DIR}/../test_scl_sync_health.cpp)
