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

add_executable(ariec61850_hard_mms_get_name_list_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_get_name_list_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/osi/session_span.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation_span.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_get_name_list_smoke)

add_executable(ariec61850_hard_mms_static_model_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_static_model_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_static_model_smoke)

add_executable(ariec61850_hard_mms_write_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_write_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/write_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_write_smoke)

add_executable(ariec61850_hard_mms_dispatcher_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_dispatcher_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/write_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_dispatcher_smoke)

add_executable(ariec61850_hard_mms_connection_runtime_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_connection_runtime_hard_profile_smoke.cpp
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
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp
    ${ARIEC61850_ROOT}/src/mms/connection_runtime.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_connection_runtime_smoke)

add_executable(ariec61850_hard_mms_data_set_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_data_set_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/write_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_data_set_smoke)

add_executable(ariec61850_hard_mms_information_report_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_information_report_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_information_report.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_information_report_smoke)

add_executable(ariec61850_hard_mms_urcb_runtime_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_urcb_runtime_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_information_report.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_runtime.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_urcb_runtime_smoke)

add_executable(ariec61850_hard_mms_urcb_objects_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_urcb_objects_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber_span_reader.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu_span.cpp
    ${ARIEC61850_ROOT}/src/mms/services_span.cpp
    ${ARIEC61850_ROOT}/src/mms/write_span.cpp
    ${ARIEC61850_ROOT}/src/mms/data_set_span.cpp
    ${ARIEC61850_ROOT}/src/mms/information_report_span.cpp
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp
    ${ARIEC61850_ROOT}/src/mms/static_information_report.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_objects.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_urcb_objects_smoke)

add_executable(ariec61850_hard_mms_report_connection_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../mms_report_connection_hard_profile_smoke.cpp
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
    ${ARIEC61850_ROOT}/src/mms/static_information_report.cpp
    ${ARIEC61850_ROOT}/src/mms/static_urcb_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/connection_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_report_connection.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_report_connection_smoke)

add_executable(ariec61850_hard_mms_server_session_smoke
    ${ARIEC61850_ROOT}/tests/test_mms_static_server_session.cpp
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
    ${ARIEC61850_ROOT}/src/mms/static_object_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_write.cpp
    ${ARIEC61850_ROOT}/src/mms/static_data_set_table.cpp
    ${ARIEC61850_ROOT}/src/mms/static_dispatcher.cpp
    ${ARIEC61850_ROOT}/src/mms/connection_runtime.cpp
    ${ARIEC61850_ROOT}/src/mms/static_server_session.cpp)
ariec61850_configure_hard_target(ariec61850_hard_mms_server_session_smoke)

enable_testing()
add_test(NAME ariec61850_hard_sv_smoke COMMAND ariec61850_hard_sv_smoke)
add_test(NAME ariec61850_hard_goose_smoke COMMAND ariec61850_hard_goose_smoke)
add_test(NAME ariec61850_hard_osi_transport_smoke COMMAND ariec61850_hard_osi_transport_smoke)
add_test(NAME ariec61850_hard_session_presentation_smoke COMMAND ariec61850_hard_session_presentation_smoke)
add_test(NAME ariec61850_hard_association_smoke COMMAND ariec61850_hard_association_smoke)
add_test(NAME ariec61850_hard_mms_pdu_smoke COMMAND ariec61850_hard_mms_pdu_smoke)
add_test(NAME ariec61850_hard_mms_get_name_list_smoke COMMAND ariec61850_hard_mms_get_name_list_smoke)
add_test(NAME ariec61850_hard_mms_static_model_smoke COMMAND ariec61850_hard_mms_static_model_smoke)
add_test(NAME ariec61850_hard_mms_write_smoke COMMAND ariec61850_hard_mms_write_smoke)
add_test(NAME ariec61850_hard_mms_dispatcher_smoke COMMAND ariec61850_hard_mms_dispatcher_smoke)
add_test(NAME ariec61850_hard_mms_connection_runtime_smoke COMMAND ariec61850_hard_mms_connection_runtime_smoke)
add_test(NAME ariec61850_hard_mms_data_set_smoke COMMAND ariec61850_hard_mms_data_set_smoke)
add_test(NAME ariec61850_hard_mms_information_report_smoke COMMAND ariec61850_hard_mms_information_report_smoke)
add_test(NAME ariec61850_hard_mms_urcb_runtime_smoke COMMAND ariec61850_hard_mms_urcb_runtime_smoke)
add_test(NAME ariec61850_hard_mms_urcb_objects_smoke COMMAND ariec61850_hard_mms_urcb_objects_smoke)
add_test(NAME ariec61850_hard_mms_report_connection_smoke COMMAND ariec61850_hard_mms_report_connection_smoke)
add_test(
    NAME ariec61850_hard_mms_server_session_smoke
    COMMAND ariec61850_hard_mms_server_session_smoke)

message(STATUS "arstack61850 hard embedded profile: exceptions disabled=ON")
message(STATUS "arstack61850 hard embedded profile: RTTI disabled=ON")
message(STATUS "arstack61850 hard embedded profile: host services excluded")
message(STATUS "arstack61850 hard embedded profile: bounded SV + GOOSE + OSI + ACSE + MMS static dispatcher/connection runtime/DataSet/InformationReport/URCB/RCB-object/report-transport gates=ON")
