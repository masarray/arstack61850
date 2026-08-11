cmake_minimum_required(VERSION 3.20)

project(ARIEC61850DynamicRcbTrial LANGUAGES CXX)

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. arstack_core)

add_executable(ariec61850_dynamic_rcb_trial
    ${CMAKE_CURRENT_LIST_DIR}/../dynamic_rcb_trial.cpp)
target_link_libraries(ariec61850_dynamic_rcb_trial PRIVATE ARIEC61850::core)
target_compile_features(ariec61850_dynamic_rcb_trial PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(ariec61850_dynamic_rcb_trial PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(ariec61850_dynamic_rcb_trial PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

if(WIN32)
    target_link_libraries(ariec61850_dynamic_rcb_trial PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME dynamic_rcb_trial_help COMMAND ariec61850_dynamic_rcb_trial --help)

add_executable(ariec61850_static_rcb_trial
    ${CMAKE_CURRENT_LIST_DIR}/../static_rcb_trial.cpp)
target_link_libraries(ariec61850_static_rcb_trial PRIVATE ARIEC61850::core)
target_compile_features(ariec61850_static_rcb_trial PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ariec61850_static_rcb_trial PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(ariec61850_static_rcb_trial PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
if(WIN32)
    target_link_libraries(ariec61850_static_rcb_trial PRIVATE ws2_32)
endif()
add_test(NAME static_rcb_trial_help COMMAND ariec61850_static_rcb_trial --help)

add_executable(ariec61850_dynamic_report_planner_tests
    ${CMAKE_CURRENT_LIST_DIR}/test_dynamic_report_planner.cpp)
target_link_libraries(ariec61850_dynamic_report_planner_tests PRIVATE
    ARIEC61850::core)
target_compile_features(ariec61850_dynamic_report_planner_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ariec61850_dynamic_report_planner_tests PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(ariec61850_dynamic_report_planner_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
add_test(NAME dynamic_report_planner_tests
    COMMAND ariec61850_dynamic_report_planner_tests)

add_executable(ariec61850_dynamic_cleanup_ownership_tests
    ${CMAKE_CURRENT_LIST_DIR}/test_dynamic_cleanup_ownership.cpp)
target_link_libraries(ariec61850_dynamic_cleanup_ownership_tests PRIVATE
    ARIEC61850::core)
target_compile_features(ariec61850_dynamic_cleanup_ownership_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ariec61850_dynamic_cleanup_ownership_tests PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(ariec61850_dynamic_cleanup_ownership_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
if(WIN32)
    target_link_libraries(ariec61850_dynamic_cleanup_ownership_tests PRIVATE ws2_32)
endif()
add_test(NAME dynamic_cleanup_ownership_tests
    COMMAND ariec61850_dynamic_cleanup_ownership_tests)
