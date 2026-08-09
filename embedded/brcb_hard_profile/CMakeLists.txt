cmake_minimum_required(VERSION 3.20)

project(ARIEC61850BrcbHardProfile
    VERSION 0.1.0
    DESCRIPTION "No-exception bounded IEC 61850 buffered-report profile"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(MSVC)
    message(FATAL_ERROR "BRCB hard profile is validated with GCC/Clang -fno-exceptions.")
endif()

function(ariec61850_configure_brcb_hard_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${ARIEC61850_ROOT}/include)
    target_compile_definitions(${target} PRIVATE
        ARIEC61850_EMBEDDED_PROFILE=1
        ARIEC61850_HOST_SERVICES=0
        ARIEC61850_NO_EXCEPTIONS=1)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Werror
        -fno-exceptions
        -fno-rtti)
endfunction()

add_executable(ariec61850_hard_mms_brcb_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_brcb_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/buffered_selective_information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_runtime.cpp)
ariec61850_configure_brcb_hard_target(ariec61850_hard_mms_brcb_smoke)

add_executable(ariec61850_hard_mms_brcb_connection_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_brcb_connection_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/osi/tpkt_span.cpp
    ${ARIEC61850_ROOT}/src/osi/cotp_span.cpp
    ${ARIEC61850_ROOT}/src/osi/session_span.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation_span.cpp
    ${ARIEC61850_ROOT}/src/acse/association_span.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/write_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp
    ${ARIEC61850_ROOT}/src/mms/connection_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/buffered_selective_information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_connection.cpp)
ariec61850_configure_brcb_hard_target(ariec61850_hard_mms_brcb_connection_smoke)

enable_testing()
add_test(NAME ariec61850_hard_mms_brcb_smoke COMMAND ariec61850_hard_mms_brcb_smoke)
add_test(
    NAME ariec61850_hard_mms_brcb_connection_smoke
    COMMAND ariec61850_hard_mms_brcb_connection_smoke)

message(STATUS "arstack61850 BRCB hard profile: bounded RAM queue + EntryID + BufOvfl=ON")
message(STATUS "arstack61850 BRCB hard profile: retry-safe reconnect delivery=ON")
message(STATUS "arstack61850 BRCB hard profile: exceptions/RTTI disabled=ON")
message(STATUS "arstack61850 BRCB hard profile: non-volatile persistence=NOT_CLAIMED")
