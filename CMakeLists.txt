cmake_minimum_required(VERSION 3.20)

project(ARIEC61850Cpp
    VERSION 0.2.0
    DESCRIPTION "Incremental C++20 port of the ARIEC61850 protocol stack"
    LANGUAGES CXX)

option(ARIEC61850_BUILD_TESTS "Build ARIEC61850 C++ regression tests" ON)
option(ARIEC61850_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(ariec61850_core
    src/asn1/ber.cpp
    src/ethernet/ethernet.cpp
    src/capture/pcap.cpp
    src/goose/retransmission_schedule.cpp
    src/mms/utc_time.cpp
    src/mms/data_value.cpp
    src/mms/data_codec.cpp)

add_library(ARIEC61850::core ALIAS ariec61850_core)

target_compile_features(ariec61850_core PUBLIC cxx_std_20)
target_include_directories(ariec61850_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if(MSVC)
    target_compile_options(ariec61850_core PRIVATE /W4 /permissive-)
    if(ARIEC61850_WARNINGS_AS_ERRORS)
        target_compile_options(ariec61850_core PRIVATE /WX)
    endif()
else()
    target_compile_options(ariec61850_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    if(ARIEC61850_WARNINGS_AS_ERRORS)
        target_compile_options(ariec61850_core PRIVATE -Werror)
    endif()
endif()

if(ARIEC61850_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(ariec61850_core_tests tests/test_main.cpp)
    target_link_libraries(ariec61850_core_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_core_tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(ariec61850_core_tests PRIVATE /W4 /permissive-)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(ariec61850_core_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(ariec61850_core_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(ariec61850_core_tests PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME ariec61850_core_tests COMMAND ariec61850_core_tests)

    add_executable(ariec61850_mms_tests tests/test_mms.cpp)
    target_link_libraries(ariec61850_mms_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_mms_tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(ariec61850_mms_tests PRIVATE /W4 /permissive-)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(ariec61850_mms_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(ariec61850_mms_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(ariec61850_mms_tests PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME ariec61850_mms_tests COMMAND ariec61850_mms_tests)
endif()
