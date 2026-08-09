cmake_minimum_required(VERSION 3.20)

project(ARIEC61850HardEmbeddedProfile
    VERSION 0.1.0
    DESCRIPTION "No-exception/no-RTTI MCU profiles for bounded process-bus and MMS server paths"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(MSVC)
    message(FATAL_ERROR "Hard embedded profiles are validated with GCC/Clang -fno-exceptions.")
endif()

function(ariec61850_configure_hard_target target)
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

add_executable(ariec61850_hard_sv_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../sv_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/ethernet/ethernet_encode.cpp
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/frame_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/sample_counter.cpp)
ariec61850_configure_hard_target(ariec61850_hard_sv_smoke)

add_executable(ariec61850_hard_goose_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../goose_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/ethernet/ethernet_encode.cpp
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/mms/data_value.cpp
    ${ARIEC61850_ROOT}/src/goose/pdu_encode.cpp
    ${ARIEC61850_ROOT}/src/goose/frame_encode.cpp)
ariec61850_configure_hard_target(ariec61850_hard_goose_smoke)

add_executable(ariec61850_hard_osi_transport_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../osi_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/osi/tpkt_span.cpp
    ${ARIEC61850_ROOT}/src/osi/cotp_span.cpp)
ariec61850_configure_hard_target(ariec61850_hard_osi_transport_smoke)

add_executable(ariec61850_hard_session_presentation_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../session_presentation_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/osi/session_span.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation_span.cpp)
ariec61850_configure_hard_target(ariec61850_hard_session_presentation_smoke)

add_executable(ariec61850_hard_association_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../association_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/osi/session_span.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation_span.cpp
    ${ARIEC61850_ROOT}/src/acse/association_span.cpp)
ariec61850_configure_hard_target(ariec61850_hard_association_smoke)

add_executable(ariec61850_hard_mms_pdu_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_pdu_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/osi/session_span.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation_span.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_pdu_smoke)

enable_testing()
add_test(NAME ariec61850_hard_sv_smoke COMMAND ariec61850_hard_sv_smoke)
add_test(NAME ariec61850_hard_goose_smoke COMMAND ariec61850_hard_goose_smoke)
add_test(NAME ariec61850_hard_osi_transport_smoke COMMAND ariec61850_hard_osi_transport_smoke)
add_test(
    NAME ariec61850_hard_session_presentation_smoke
    COMMAND ariec61850_hard_session_presentation_smoke)
add_test(
    NAME ariec61850_hard_association_smoke
    COMMAND ariec61850_hard_association_smoke)
add_test(
    NAME ariec61850_hard_mms_pdu_smoke
    COMMAND ariec61850_hard_mms_pdu_smoke)

message(STATUS "arstack61850 hard embedded profile: exceptions disabled=ON")
message(STATUS "arstack61850 hard embedded profile: RTTI disabled=ON")
message(STATUS "arstack61850 hard embedded profile: host services excluded")
message(STATUS "arstack61850 hard embedded profile: bounded SV + GOOSE + OSI + ACSE + MMS PDU gates=ON")
