cmake_minimum_required(VERSION 3.20)
project(ARStackSmvGuiHost LANGUAGES CXX)

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../.. arstack-core)

add_executable(ariec61850_smv_profile_inspect
    ${CMAKE_CURRENT_LIST_DIR}/../../../tools/smv_profile_inspect.cpp)
target_link_libraries(ariec61850_smv_profile_inspect PRIVATE ARIEC61850::core)
target_compile_features(ariec61850_smv_profile_inspect PRIVATE cxx_std_20)
