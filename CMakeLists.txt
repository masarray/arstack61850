cmake_minimum_required(VERSION 3.20)

project(ARIEC61850Cpp
    VERSION 0.4.0
    DESCRIPTION "Incremental C++20 port of the ARIEC61850 protocol stack"
    LANGUAGES CXX)

option(ARIEC61850_BUILD_TESTS "Build ARIEC61850 C++ regression tests" ON)
option(ARIEC61850_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(ariec61850_core
    src/asn1/ber.cpp
    src/ethernet/ethernet.cpp
    src/capture/pcap.cpp
    src/goose/retransmission_schedule.cpp
    src/goose/pdu_codec.cpp
    src/goose/frame_codec.cpp
    src/goose/subscriber_supervisor.cpp
    src/goose/publisher_runtime.cpp
    src/mms/utc_time.cpp
    src/mms/data_value.cpp
    src/mms/data_codec.cpp)

add_library(ARIEC61850::core ALIAS ariec61850_core)

target_compile_features(ariec61850_core PUBLIC cxx_std_20)
target_include_directories(ariec61850_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

function(ariec61850_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

ariec61850_apply_warnings(ariec61850_core)

if(ARIEC61850_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(ariec61850_core_tests tests/test_main.cpp)
    target_link_libraries(ariec61850_core_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_core_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_core_tests)
    add_test(NAME ariec61850_core_tests COMMAND ariec61850_core_tests)

    foreach(test_name IN ITEMS mms goose)
        add_executable(ariec61850_${test_name}_tests tests/test_${test_name}.cpp)
        target_link_libraries(ariec61850_${test_name}_tests PRIVATE ARIEC61850::core)
        target_compile_features(ariec61850_${test_name}_tests PRIVATE cxx_std_20)
        ariec61850_apply_warnings(ariec61850_${test_name}_tests)
        add_test(NAME ariec61850_${test_name}_tests COMMAND ariec61850_${test_name}_tests)
    endforeach()

    add_executable(ariec61850_goose_runtime_tests tests/test_goose_runtime.cpp)
    target_link_libraries(ariec61850_goose_runtime_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_goose_runtime_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_goose_runtime_tests)
    add_test(NAME ariec61850_goose_runtime_tests COMMAND ariec61850_goose_runtime_tests)
endif()
