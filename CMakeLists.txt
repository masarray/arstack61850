cmake_minimum_required(VERSION 3.20)

project(ARIEC61850Cpp
    VERSION 0.14.0
    DESCRIPTION "Incremental C++20 port of the ARIEC61850 protocol stack"
    LANGUAGES CXX)

option(ARIEC61850_BUILD_TESTS "Build ARIEC61850 C++ regression tests" ON)
option(ARIEC61850_BUILD_TOOLS "Build read-only evidence tools" ON)
option(ARIEC61850_BUILD_FUZZERS "Build LLVM libFuzzer targets" OFF)
option(ARIEC61850_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
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
    src/mms/data_codec.cpp
    src/mms/pdu.cpp
    src/mms/services.cpp
    src/mms/file_service.cpp
    src/mms/dynamic_data_set.cpp
    src/mms/dynamic_report_planner.cpp
    src/mms/invoke_router.cpp
    src/mms/reporting.cpp
    src/mms/association_runtime.cpp
    src/mms/report_subscription_runtime.cpp
    src/mms/tcp_transport.cpp
    src/mms/live_discovery.cpp
    src/mms/rcb_selection.cpp
    src/mms/rcb_failover.cpp
    src/sampled_values/pdu_codec.cpp
    src/sampled_values/frame_codec.cpp
    src/sampled_values/quality.cpp
    src/sampled_values/payload_inspector.cpp
    src/sampled_values/sample_counter.cpp
    src/sampled_values/stream_supervisor.cpp
    src/evidence/pcap_equivalence.cpp
    src/scl/dataset_reference.cpp
    src/scl/parser.cpp
    src/comtrade/model.cpp
    src/comtrade/reader.cpp
    src/comtrade/channel_mapper.cpp
    src/comtrade/scl_mapper.cpp
    src/osi/tpkt.cpp
    src/osi/cotp.cpp
    src/osi/session.cpp
    src/osi/presentation.cpp
    src/acse/association.cpp)

add_library(ARIEC61850::core ALIAS ariec61850_core)

target_compile_features(ariec61850_core PUBLIC cxx_std_20)
target_include_directories(ariec61850_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if(WIN32)
    target_link_libraries(ariec61850_core PRIVATE ws2_32)
endif()

function(ariec61850_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        if(ARIEC61850_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(ariec61850_apply_sanitizers target)
    if(NOT ARIEC61850_ENABLE_SANITIZERS)
        return()
    endif()
    if(MSVC)
        message(FATAL_ERROR "ARIEC61850_ENABLE_SANITIZERS currently requires GCC or Clang.")
    endif()
    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
endfunction()

ariec61850_apply_warnings(ariec61850_core)
ariec61850_apply_sanitizers(ariec61850_core)

if(ARIEC61850_BUILD_TOOLS)
    add_executable(ariec61850_pcap_interop_check tools/pcap_interop_check.cpp)
    target_link_libraries(ariec61850_pcap_interop_check PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_pcap_interop_check PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_pcap_interop_check)
    ariec61850_apply_sanitizers(ariec61850_pcap_interop_check)

    add_executable(ariec61850_comtrade_inspect tools/comtrade_inspect.cpp)
    target_link_libraries(ariec61850_comtrade_inspect PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_comtrade_inspect PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_comtrade_inspect)
    ariec61850_apply_sanitizers(ariec61850_comtrade_inspect)

    add_executable(ariec61850_live_discover tools/live_discover.cpp)
    target_link_libraries(ariec61850_live_discover PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_live_discover PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_live_discover)
    ariec61850_apply_sanitizers(ariec61850_live_discover)

    add_executable(ariec61850_rcb_contention_probe tools/rcb_contention_probe.cpp)
    target_link_libraries(ariec61850_rcb_contention_probe PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_rcb_contention_probe PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_rcb_contention_probe)
    ariec61850_apply_sanitizers(ariec61850_rcb_contention_probe)

    add_executable(ariec61850_control_block_read_probe tools/control_block_read_probe.cpp)
    target_link_libraries(ariec61850_control_block_read_probe PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_control_block_read_probe PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_control_block_read_probe)
    ariec61850_apply_sanitizers(ariec61850_control_block_read_probe)

    add_executable(ariec61850_pagination_probe tools/pagination_probe.cpp)
    target_link_libraries(ariec61850_pagination_probe PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_pagination_probe PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_pagination_probe)
    ariec61850_apply_sanitizers(ariec61850_pagination_probe)

    add_executable(ariec61850_file_transfer tools/file_transfer.cpp)
    target_link_libraries(ariec61850_file_transfer PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_file_transfer PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_file_transfer)
    ariec61850_apply_sanitizers(ariec61850_file_transfer)
endif()

if(ARIEC61850_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(ariec61850_core_tests tests/test_main.cpp)
    target_link_libraries(ariec61850_core_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_core_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_core_tests)
    ariec61850_apply_sanitizers(ariec61850_core_tests)
    add_test(NAME ariec61850_core_tests COMMAND ariec61850_core_tests)

    foreach(test_name IN ITEMS mms goose sampled_values)
        add_executable(ariec61850_${test_name}_tests tests/test_${test_name}.cpp)
        target_link_libraries(ariec61850_${test_name}_tests PRIVATE ARIEC61850::core)
        target_compile_features(ariec61850_${test_name}_tests PRIVATE cxx_std_20)
        ariec61850_apply_warnings(ariec61850_${test_name}_tests)
        ariec61850_apply_sanitizers(ariec61850_${test_name}_tests)
        add_test(NAME ariec61850_${test_name}_tests COMMAND ariec61850_${test_name}_tests)
    endforeach()

    add_executable(ariec61850_goose_runtime_tests tests/test_goose_runtime.cpp)
    target_link_libraries(ariec61850_goose_runtime_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_goose_runtime_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_goose_runtime_tests)
    ariec61850_apply_sanitizers(ariec61850_goose_runtime_tests)
    add_test(NAME ariec61850_goose_runtime_tests COMMAND ariec61850_goose_runtime_tests)

    add_executable(ariec61850_evidence_tests tests/test_evidence.cpp)
    target_link_libraries(ariec61850_evidence_tests PRIVATE ARIEC61850::core)
    target_compile_definitions(ariec61850_evidence_tests PRIVATE
        ARIEC61850_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ariec61850_evidence_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_evidence_tests)
    ariec61850_apply_sanitizers(ariec61850_evidence_tests)
    add_test(NAME ariec61850_evidence_tests COMMAND ariec61850_evidence_tests)

    add_executable(ariec61850_scl_tests tests/test_scl.cpp)
    target_link_libraries(ariec61850_scl_tests PRIVATE ARIEC61850::core)
    target_compile_definitions(ariec61850_scl_tests PRIVATE
        ARIEC61850_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ariec61850_scl_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_scl_tests)
    ariec61850_apply_sanitizers(ariec61850_scl_tests)
    add_test(NAME ariec61850_scl_tests COMMAND ariec61850_scl_tests)

    add_executable(ariec61850_comtrade_tests tests/test_comtrade.cpp)
    target_link_libraries(ariec61850_comtrade_tests PRIVATE ARIEC61850::core)
    target_compile_definitions(ariec61850_comtrade_tests PRIVATE
        ARIEC61850_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ariec61850_comtrade_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_comtrade_tests)
    ariec61850_apply_sanitizers(ariec61850_comtrade_tests)
    add_test(NAME ariec61850_comtrade_tests COMMAND ariec61850_comtrade_tests)

    add_executable(ariec61850_osi_transport_tests tests/test_osi_transport.cpp)
    target_link_libraries(ariec61850_osi_transport_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_osi_transport_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_osi_transport_tests)
    ariec61850_apply_sanitizers(ariec61850_osi_transport_tests)
    add_test(NAME ariec61850_osi_transport_tests COMMAND ariec61850_osi_transport_tests)

    add_executable(ariec61850_osi_association_tests tests/test_osi_association.cpp)
    target_link_libraries(ariec61850_osi_association_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_osi_association_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_osi_association_tests)
    ariec61850_apply_sanitizers(ariec61850_osi_association_tests)
    add_test(NAME ariec61850_osi_association_tests COMMAND ariec61850_osi_association_tests)

    add_executable(ariec61850_mms_services_tests tests/test_mms_services.cpp)
    target_link_libraries(ariec61850_mms_services_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_mms_services_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_mms_services_tests)
    ariec61850_apply_sanitizers(ariec61850_mms_services_tests)
    add_test(NAME ariec61850_mms_services_tests COMMAND ariec61850_mms_services_tests)

    add_executable(ariec61850_mms_file_service_tests tests/test_mms_file_service.cpp)
    target_link_libraries(ariec61850_mms_file_service_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_mms_file_service_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_mms_file_service_tests)
    ariec61850_apply_sanitizers(ariec61850_mms_file_service_tests)
    add_test(NAME ariec61850_mms_file_service_tests COMMAND ariec61850_mms_file_service_tests)

    add_executable(ariec61850_dynamic_data_set_tests tests/test_dynamic_data_set.cpp)
    target_link_libraries(ariec61850_dynamic_data_set_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_dynamic_data_set_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_dynamic_data_set_tests)
    ariec61850_apply_sanitizers(ariec61850_dynamic_data_set_tests)
    add_test(NAME ariec61850_dynamic_data_set_tests COMMAND ariec61850_dynamic_data_set_tests)

    add_executable(ariec61850_rcb_failover_tests tests/test_rcb_failover.cpp)
    target_link_libraries(ariec61850_rcb_failover_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_rcb_failover_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_rcb_failover_tests)
    ariec61850_apply_sanitizers(ariec61850_rcb_failover_tests)
    add_test(NAME ariec61850_rcb_failover_tests COMMAND ariec61850_rcb_failover_tests)

    add_executable(ariec61850_reporting_tests tests/test_reporting.cpp)
    target_link_libraries(ariec61850_reporting_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_reporting_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_reporting_tests)
    ariec61850_apply_sanitizers(ariec61850_reporting_tests)
    add_test(NAME ariec61850_reporting_tests COMMAND ariec61850_reporting_tests)

    add_executable(ariec61850_mms_runtime_tests tests/test_mms_runtime.cpp)
    target_link_libraries(ariec61850_mms_runtime_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_mms_runtime_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_mms_runtime_tests)
    ariec61850_apply_sanitizers(ariec61850_mms_runtime_tests)
    add_test(NAME ariec61850_mms_runtime_tests COMMAND ariec61850_mms_runtime_tests)

    add_executable(ariec61850_tcp_transport_tests tests/test_tcp_transport.cpp)
    target_link_libraries(ariec61850_tcp_transport_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_tcp_transport_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_tcp_transport_tests)
    ariec61850_apply_sanitizers(ariec61850_tcp_transport_tests)
    add_test(NAME ariec61850_tcp_transport_tests COMMAND ariec61850_tcp_transport_tests)

    add_executable(ariec61850_live_discovery_tests tests/test_live_discovery.cpp)
    target_link_libraries(ariec61850_live_discovery_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_live_discovery_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_live_discovery_tests)
    ariec61850_apply_sanitizers(ariec61850_live_discovery_tests)
    add_test(NAME ariec61850_live_discovery_tests COMMAND ariec61850_live_discovery_tests)

    add_executable(ariec61850_rcb_selection_tests tests/test_rcb_selection.cpp)
    target_link_libraries(ariec61850_rcb_selection_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_rcb_selection_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_rcb_selection_tests)
    ariec61850_apply_sanitizers(ariec61850_rcb_selection_tests)
    add_test(NAME ariec61850_rcb_selection_tests COMMAND ariec61850_rcb_selection_tests)

    add_executable(ariec61850_control_block_read_tests tests/test_control_block_read.cpp)
    target_link_libraries(ariec61850_control_block_read_tests PRIVATE ARIEC61850::core)
    target_compile_features(ariec61850_control_block_read_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_control_block_read_tests)
    ariec61850_apply_sanitizers(ariec61850_control_block_read_tests)
    add_test(NAME ariec61850_control_block_read_tests COMMAND ariec61850_control_block_read_tests)

    add_executable(ariec61850_fuzz_smoke_tests tests/test_fuzz_smoke.cpp)
    target_link_libraries(ariec61850_fuzz_smoke_tests PRIVATE ARIEC61850::core)
    target_include_directories(ariec61850_fuzz_smoke_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
    target_compile_definitions(ariec61850_fuzz_smoke_tests PRIVATE
        ARIEC61850_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ariec61850_fuzz_smoke_tests PRIVATE cxx_std_20)
    ariec61850_apply_warnings(ariec61850_fuzz_smoke_tests)
    ariec61850_apply_sanitizers(ariec61850_fuzz_smoke_tests)
    add_test(NAME ariec61850_fuzz_smoke_tests COMMAND ariec61850_fuzz_smoke_tests)

    if(ARIEC61850_BUILD_TOOLS)
        add_test(
            NAME ariec61850_pcap_interop_smoke
            COMMAND ariec61850_pcap_interop_check
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/process_bus_csharp_vectors.pcap
                --json)
        add_test(
            NAME ariec61850_comtrade_inspect_smoke
            COMMAND ariec61850_comtrade_inspect
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/comtrade/simple_fault.cfg
                --json)
        add_test(
            NAME ariec61850_pagination_probe_help
            COMMAND ariec61850_pagination_probe --help)
        add_test(
            NAME ariec61850_file_transfer_help
            COMMAND ariec61850_file_transfer --help)
    endif()
endif()

if(ARIEC61850_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ARIEC61850_BUILD_FUZZERS requires Clang/libFuzzer.")
    endif()

    function(ariec61850_add_fuzzer name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE ARIEC61850::core)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_compile_options(${name} PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        target_link_options(${name} PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        ariec61850_apply_warnings(${name})
    endfunction()

    ariec61850_add_fuzzer(ariec61850_fuzz_ber fuzz/fuzz_ber.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_goose fuzz/fuzz_goose.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_sampled_values fuzz/fuzz_sampled_values.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_pcap fuzz/fuzz_pcap.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_scl fuzz/fuzz_scl.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_comtrade fuzz/fuzz_comtrade.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_transport fuzz/fuzz_transport.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_association fuzz/fuzz_association.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_mms_services fuzz/fuzz_mms_services.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_mms_file_service fuzz/fuzz_mms_file_service.cpp)
    ariec61850_add_fuzzer(ariec61850_fuzz_reporting fuzz/fuzz_reporting.cpp)
endif()
