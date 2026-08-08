cmake_minimum_required(VERSION 3.20)

project(ARStack61850SvReplayPrepare LANGUAGES CXX)

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_BINARY_DIR}/arstack-core"
    EXCLUDE_FROM_ALL)

add_executable(
    ariec61850_sv_replay_prepare
    "${CMAKE_CURRENT_LIST_DIR}/../sv_replay_prepare.cpp")
target_link_libraries(ariec61850_sv_replay_prepare PRIVATE ARIEC61850::core)
target_compile_features(ariec61850_sv_replay_prepare PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(ariec61850_sv_replay_prepare PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(ariec61850_sv_replay_prepare PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
