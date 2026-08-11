cmake_minimum_required(VERSION 3.20)

project(ARIEC61850ControlMmsProfile
    VERSION 0.1.0
    DESCRIPTION "Live-type IEC 61850 Oper/SBOw/Cancel MMS structure validation"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(ariec61850_control_mms_structure_tests
    ${ARIEC61850_ROOT}/tests/test_control_mms_structure.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber.cpp
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/mms/data_value.cpp
    ${ARIEC61850_ROOT}/src/mms/data_codec.cpp
    ${ARIEC61850_ROOT}/src/control/guarded_control.cpp
    ${ARIEC61850_ROOT}/src/control/mms_control_structure.cpp)

target_compile_features(ariec61850_control_mms_structure_tests PRIVATE cxx_std_20)
target_include_directories(ariec61850_control_mms_structure_tests PRIVATE
    ${ARIEC61850_ROOT}/include)

if(MSVC)
    target_compile_options(ariec61850_control_mms_structure_tests PRIVATE
        /W4 /WX /permissive-)
else()
    target_compile_options(ariec61850_control_mms_structure_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

enable_testing()
add_test(
    NAME ariec61850_control_mms_structure_tests
    COMMAND ariec61850_control_mms_structure_tests)

message(STATUS "arstack61850 Control MMS profile: live TypeSpecification binding=ON")
message(STATUS "arstack61850 Control MMS profile: Oper/SBOw/Cancel exact fields=ON")
message(STATUS "arstack61850 Control MMS profile: DPC network-bit-order oracle=ON")
message(STATUS "arstack61850 Control MMS profile: Check 2-bit oracle=ON")
message(STATUS "arstack61850 Control MMS profile: vendor-specific field guessing=OFF")
