cmake_minimum_required(VERSION 3.20)

project(ARIEC61850UrcbEventHardProfile
    VERSION 0.1.0
    DESCRIPTION "No-exception bounded IEC 61850 URCB event-report profile"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(MSVC)
    message(FATAL_ERROR "URCB event hard profile is validated with GCC/Clang -fno-exceptions.")
endif()

add_executable(ariec61850_hard_mms_urcb_event_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_urcb_event_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_information_report.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/selective_information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_event_runtime.cpp)

target_compile_features(ariec61850_hard_mms_urcb_event_smoke PRIVATE cxx_std_20)
target_include_directories(ariec61850_hard_mms_urcb_event_smoke PRIVATE ${ARIEC61850_ROOT}/include)
target_compile_definitions(ariec61850_hard_mms_urcb_event_smoke PRIVATE
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0
    ARIEC61850_NO_EXCEPTIONS=1)
target_compile_options(ariec61850_hard_mms_urcb_event_smoke PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Werror
    -fno-exceptions
    -fno-rtti)

enable_testing()
add_test(
    NAME ariec61850_hard_mms_urcb_event_smoke
    COMMAND ariec61850_hard_mms_urcb_event_smoke)

message(STATUS "arstack61850 URCB event hard profile: dchg/qchg/dupd + BufTm=ON")
message(STATUS "arstack61850 URCB event hard profile: exceptions/RTTI disabled=ON")
