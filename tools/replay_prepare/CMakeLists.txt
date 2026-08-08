cmake_minimum_required(VERSION 3.20)

project(ARStack61850SvReplayTools LANGUAGES CXX)

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_BINARY_DIR}/arstack-core"
    EXCLUDE_FROM_ALL)

function(arstack_replay_tool target source)
    add_executable(${target} "${source}")
    target_link_libraries(${target} PRIVATE ARIEC61850::core)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endfunction()

arstack_replay_tool(
    ariec61850_sv_replay_prepare
    "${CMAKE_CURRENT_LIST_DIR}/../sv_replay_prepare.cpp")
arstack_replay_tool(
    ariec61850_sv_replay_live
    "${CMAKE_CURRENT_LIST_DIR}/../sv_replay_live.cpp")
