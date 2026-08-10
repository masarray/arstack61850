cmake_minimum_required(VERSION 3.20)

project(ARIEC61850ControlTerminationProfile
    VERSION 0.1.0
    DESCRIPTION "IEC 61850 CommandTermination and LastApplError parity validation"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(ariec61850_command_termination_tests
    ${ARIEC61850_ROOT}/tests/test_command_termination.cpp
    ${ARIEC61850_ROOT}/src/mms/data_value.cpp
    ${ARIEC61850_ROOT}/src/control/guarded_control.cpp
    ${ARIEC61850_ROOT}/src/control/command_termination.cpp)

target_compile_features(ariec61850_command_termination_tests PRIVATE cxx_std_20)
target_include_directories(ariec61850_command_termination_tests PRIVATE
    ${ARIEC61850_ROOT}/include)

if(MSVC)
    target_compile_options(ariec61850_command_termination_tests PRIVATE
        /W4 /WX /permissive-)
else()
    target_compile_options(ariec61850_command_termination_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

enable_testing()
add_test(
    NAME ariec61850_command_termination_tests
    COMMAND ariec61850_command_termination_tests)

message(STATUS "arstack61850 Control termination profile: LastApplError mapping=ON")
message(STATUS "arstack61850 Control termination profile: exact CO/Oper positive boundary=ON")
message(STATUS "arstack61850 Control termination profile: ordinary ST/MX completion=DENIED")
message(STATUS "arstack61850 Control termination profile: unknown numeric diagnostics preserved=ON")
