# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

option(ARIEC61850_EMBEDDED_NO_EXCEPTIONS "Build embedded profile without C++ exceptions" ON)
option(ARIEC61850_EMBEDDED_NO_RTTI "Build embedded profile without RTTI" ON)
option(ARIEC61850_EMBEDDED_WARNINGS_AS_ERRORS "Treat embedded warnings as errors" ON)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

set(ARIEC61850_EMBEDDED_SOURCES
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/osi/tpkt_span.cpp
    ${ARIEC61850_ROOT}/src/osi/cotp_span.cpp
    ${ARIEC61850_ROOT}/src/osi/session_span.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation_span.cpp
    ${ARIEC61850_ROOT}/src/acse/association_span.cpp
    ${ARIEC61850_ROOT}/src/acse/association_compat_span.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_compat_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/write_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/buffered_selective_information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_direct_control.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp
    ${ARIEC61850_ROOT}/src/mms/connection_runtime_iedsim.cpp
    ${ARIEC61850_ROOT}/src/mms/static_server_session.cpp
    ${ARIEC61850_ROOT}/src/mms/static_information_report.cpp
    ${ARIEC61850_ROOT}/src/mms/static_report_connection.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_objects.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_control.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_objects.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_connection.cpp
    ${ARIEC61850_ROOT}/src/mms/static_brcb_state_codec.cpp)

add_library(ariec61850_embedded_core STATIC ${ARIEC61850_EMBEDDED_SOURCES})
add_library(ARIEC61850::embedded_core ALIAS ariec61850_embedded_core)
target_include_directories(ariec61850_embedded_core PUBLIC ${ARIEC61850_ROOT}/include)
target_compile_features(ariec61850_embedded_core PUBLIC cxx_std_20)
target_compile_definitions(ariec61850_embedded_core PUBLIC
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0)

function(ariec61850_apply_embedded_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(ARIEC61850_EMBEDDED_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        if(ARIEC61850_EMBEDDED_NO_RTTI)
            target_compile_options(${target} PRIVATE /GR-)
        endif()
        if(ARIEC61850_EMBEDDED_NO_EXCEPTIONS)
            target_compile_options(${target} PRIVATE /EHs-c-)
            target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
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
        mms_iedscout_bulk_read
        mms_iedscout_pagination
        mms_iedscout_fc_root_read
        mms_iedscout_rcb_root
        mms_iedscout_rcb_control
        mms_urcb_event
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
