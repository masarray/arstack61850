cmake_minimum_required(VERSION 3.20)

project(ARIEC61850EmbeddedCore
    VERSION 0.1.0
    DESCRIPTION "MCU-oriented build profile for arstack61850 wire protocols"
    LANGUAGES CXX)

option(ARIEC61850_EMBEDDED_NO_RTTI "Disable C++ RTTI in the embedded profile" ON)
option(ARIEC61850_EMBEDDED_NO_EXCEPTIONS "Disable C++ exceptions after legacy throw paths are removed" OFF)
option(ARIEC61850_EMBEDDED_WARNINGS_AS_ERRORS "Treat embedded-profile warnings as errors" ON)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# Keep this source list deliberately small. It is the MCU portability boundary:
# wire codecs and protocol state only, with no TCP host transport, live discovery,
# PCAP, COMTRADE, SCL parsing, filesystem, JSON or CLI tooling.
set(ARIEC61850_EMBEDDED_SOURCES
    ${ARIEC61850_ROOT}/src/asn1/ber.cpp
    ${ARIEC61850_ROOT}/src/ethernet/ethernet.cpp

    ${ARIEC61850_ROOT}/src/goose/retransmission_schedule.cpp
    ${ARIEC61850_ROOT}/src/goose/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/goose/frame_codec.cpp

    ${ARIEC61850_ROOT}/src/sampled_values/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/frame_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/quality.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/payload_inspector.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/sample_counter.cpp

    ${ARIEC61850_ROOT}/src/osi/tpkt.cpp
    ${ARIEC61850_ROOT}/src/osi/cotp.cpp
    ${ARIEC61850_ROOT}/src/osi/session.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation.cpp
    ${ARIEC61850_ROOT}/src/acse/association.cpp

    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/osi/tpkt_span.cpp
    ${ARIEC61850_ROOT}/src/osi/cotp_span.cpp
    ${ARIEC61850_ROOT}/src/osi/session_span.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation_span.cpp
    ${ARIEC61850_ROOT}/src/acse/association_span.cpp

    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/mms/data_value.cpp
    ${ARIEC61850_ROOT}/src/mms/data_codec.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu.cpp
    ${ARIEC61850_ROOT}/src/mms/services.cpp
    ${ARIEC61850_ROOT}/src/mms/invoke_router.cpp
    ${ARIEC61850_ROOT}/src/mms/association_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/file_service.cpp

    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/write_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/buffered_selective_information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp
    ${ARIEC61850_ROOT}/src/mms/connection_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_server_session.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_control.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_objects.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_connection.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_checkpoint_store.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_state_codec.cpp)

add_library(ariec61850_embedded_core STATIC ${ARIEC61850_EMBEDDED_SOURCES})
add_library(ARIEC61850::embedded_core ALIAS ariec61850_embedded_core)

target_compile_features(ariec61850_embedded_core PUBLIC cxx_std_20)
target_include_directories(ariec61850_embedded_core PUBLIC ${ARIEC61850_ROOT}/include)
target_compile_definitions(ariec61850_embedded_core PUBLIC
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0)

function(ariec61850_apply_embedded_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(ARIEC61850_EMBEDDED_NO_RTTI)
            target_compile_options(${target} PRIVATE /GR-)
        endif()
        if(ARIEC61850_EMBEDDED_NO_EXCEPTIONS)
            message(FATAL_ERROR
                "ARIEC61850_EMBEDDED_NO_EXCEPTIONS is not ready on MSVC yet. "
                "Legacy protocol error paths still use C++ exceptions.")
        endif()
        if(ARIEC61850_EMBEDDED_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        if(ARIEC61850_EMBEDDED_NO_RTTI)
            target_compile_options(${target} PRIVATE -fno-rtti)
        endif()
        if(ARIEC61850_EMBEDDED_NO_EXCEPTIONS)
            target_compile_options(${target} PRIVATE -fno-exceptions)
        endif()
        if(ARIEC61850_EMBEDDED_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

ariec61850_apply_embedded_flags(ariec61850_embedded_core)

# Link every embedded source into one executable. This catches accidental
# dependencies on host-only objects that a static-library-only build can hide.
add_executable(ariec61850_embedded_link_smoke
    ${CMAKE_CURRENT_LIST_DIR}/embedded_link_smoke.cpp
    ${ARIEC61850_EMBEDDED_SOURCES})
target_compile_features(ariec61850_embedded_link_smoke PRIVATE cxx_std_20)
target_include_directories(ariec61850_embedded_link_smoke PRIVATE ${ARIEC61850_ROOT}/include)
target_compile_definitions(ariec61850_embedded_link_smoke PRIVATE
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0)
ariec61850_apply_embedded_flags(ariec61850_embedded_link_smoke)

enable_testing()
add_test(NAME ariec61850_embedded_link_smoke COMMAND ariec61850_embedded_link_smoke)

foreach(server_smoke IN ITEMS
        osi
        session_presentation
        association
        mms_pdu
        mms_get_name_list
        mms_static_model
        mms_write
        mms_dispatcher
        mms_connection_runtime
        mms_negotiated_limits
        mms_data_set
        mms_brcb
        mms_brcb_control
        mms_brcb_connection
        mms_brcb_objects
        mms_brcb_lifecycle
        mms_brcb_persistence
        mms_brcb_recovery_v2)
    set(server_smoke_target "ariec61850_embedded_${server_smoke}_smoke")
    add_executable(
        ${server_smoke_target}
        ${CMAKE_CURRENT_LIST_DIR}/${server_smoke}_hard_profile_smoke.cpp)
    target_link_libraries(
        ${server_smoke_target} PRIVATE ARIEC61850::embedded_core)
    target_compile_features(${server_smoke_target} PRIVATE cxx_std_20)
    ariec61850_apply_embedded_flags(${server_smoke_target})
    add_test(NAME ${server_smoke_target} COMMAND ${server_smoke_target})
endforeach()

message(STATUS "arstack61850 embedded profile: host services excluded")
message(STATUS "arstack61850 embedded profile: RTTI disabled=${ARIEC61850_EMBEDDED_NO_RTTI}")
message(STATUS "arstack61850 embedded profile: exceptions disabled=${ARIEC61850_EMBEDDED_NO_EXCEPTIONS}")
