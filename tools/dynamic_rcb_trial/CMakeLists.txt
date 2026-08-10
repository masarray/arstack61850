cmake_minimum_required(VERSION 3.20)

project(ARIEC61850DynamicRcbTrial LANGUAGES CXX)

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. arstack_core)

add_library(ariec61850_dynamic_dataset_trial_support STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/dynamic_data_set.cpp)
target_link_libraries(ariec61850_dynamic_dataset_trial_support PUBLIC ARIEC61850::core)
target_compile_features(ariec61850_dynamic_dataset_trial_support PUBLIC cxx_std_20)
target_include_directories(ariec61850_dynamic_dataset_trial_support PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../include)

add_executable(ariec61850_dynamic_rcb_trial
    ${CMAKE_CURRENT_LIST_DIR}/../dynamic_rcb_trial.cpp)
target_link_libraries(ariec61850_dynamic_rcb_trial PRIVATE
    ARIEC61850::core
    ariec61850_dynamic_dataset_trial_support)
target_compile_features(ariec61850_dynamic_rcb_trial PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(ariec61850_dynamic_dataset_trial_support PRIVATE /W4 /permissive- /WX)
    target_compile_options(ariec61850_dynamic_rcb_trial PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(ariec61850_dynamic_dataset_trial_support PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    target_compile_options(ariec61850_dynamic_rcb_trial PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

if(WIN32)
    target_link_libraries(ariec61850_dynamic_rcb_trial PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME dynamic_rcb_trial_help COMMAND ariec61850_dynamic_rcb_trial --help)
