cmake_minimum_required(VERSION 3.20)

project(ARStack61850MmsR12ServerProfile LANGUAGES CXX)

get_filename_component(ARSTACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_WARNINGS_AS_ERRORS ON CACHE BOOL "" FORCE)

# Keep the repository's regular targets available, but do not link the public
# MMS server core into this A/B profile. The profile below compiles the current
# codecs with the isolated IED-simulator parity runtime instead.
add_subdirectory("${ARSTACK_ROOT}" arstack-core)

function(server_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endfunction()

add_library(ariec61850_mms_r12_server_core STATIC
    "${ARSTACK_ROOT}/src/asn1/ber_span_reader.cpp"
    "${ARSTACK_ROOT}/src/osi/tpkt_span.cpp"
    "${ARSTACK_ROOT}/src/osi/cotp_span.cpp"
    "${ARSTACK_ROOT}/src/osi/session_span.cpp"
    "${ARSTACK_ROOT}/src/osi/presentation_span.cpp"
    "${ARSTACK_ROOT}/src/acse/association_span.cpp"
    "${ARSTACK_ROOT}/src/acse/association_compat_span.cpp"
    "${ARSTACK_ROOT}/src/mms/pdu_span.cpp"
    "${ARSTACK_ROOT}/src/mms/pdu_compat_span.cpp"
    "${ARSTACK_ROOT}/src/mms/services_span.cpp"
    "${ARSTACK_ROOT}/src/mms/write_span.cpp"
    "${ARSTACK_ROOT}/src/mms/data_set_span.cpp"
    "${ARSTACK_ROOT}/src/mms/information_report_span.cpp"
    "${ARSTACK_ROOT}/src/mms/static_object_table.cpp"
    "${ARSTACK_ROOT}/src/mms/static_write.cpp"
    "${ARSTACK_ROOT}/src/mms/static_data_set_table.cpp"
    "${ARSTACK_ROOT}/src/mms/static_dispatcher.cpp"
    "${ARSTACK_ROOT}/src/mms/connection_runtime_iedsim.cpp"
    "${ARSTACK_ROOT}/src/mms/static_server_session.cpp"
    "${ARSTACK_ROOT}/src/mms/static_direct_control.cpp"
    "${ARSTACK_ROOT}/src/mms/static_information_report.cpp"
    "${ARSTACK_ROOT}/src/mms/static_report_connection.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_objects.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_runtime.cpp")
target_include_directories(ariec61850_mms_r12_server_core PUBLIC
    "${ARSTACK_ROOT}/include")
target_compile_features(ariec61850_mms_r12_server_core PUBLIC cxx_std_20)
server_strict_warnings(ariec61850_mms_r12_server_core)

add_executable(ariec61850_mms_direct_control_server
    "${ARSTACK_ROOT}/tools/mms_direct_control_server.cpp")
target_link_libraries(ariec61850_mms_direct_control_server PRIVATE
    ariec61850_mms_r12_server_core
    ARIEC61850::core)
if(WIN32)
    target_link_libraries(ariec61850_mms_direct_control_server PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_mms_direct_control_server PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_direct_control_server)

add_executable(ariec61850_mms_urcb_server
    "${ARSTACK_ROOT}/tools/mms_urcb_server.cpp")
target_link_libraries(ariec61850_mms_urcb_server PRIVATE
    ariec61850_mms_r12_server_core
    ARIEC61850::core)
if(WIN32)
    target_link_libraries(ariec61850_mms_urcb_server PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_mms_urcb_server PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_urcb_server)

# Build the repository's operational discovery client inside this isolated
# profile. The loopback CTest below is the integration contract between the
# runnable servers and the client path used by current engineering workflows.
add_executable(ariec61850_mms_live_discover_client
    "${ARSTACK_ROOT}/tools/live_discover.cpp")
target_link_libraries(ariec61850_mms_live_discover_client PRIVATE
    ARIEC61850::core)
target_compile_features(ariec61850_mms_live_discover_client PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_live_discover_client)

add_executable(ariec61850_mms_control_inventory_client
    "${ARSTACK_ROOT}/tools/control_inventory_probe.cpp")
target_link_libraries(ariec61850_mms_control_inventory_client PRIVATE
    ARIEC61850::core)
target_compile_features(ariec61850_mms_control_inventory_client PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_control_inventory_client)

add_executable(ariec61850_mms_control_interop_client
    "${ARSTACK_ROOT}/tools/control_interop_probe.cpp")
target_link_libraries(ariec61850_mms_control_interop_client PRIVATE
    ARIEC61850::core)
target_compile_features(ariec61850_mms_control_interop_client PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_control_interop_client)

include(CTest)
enable_testing()

add_executable(ariec61850_mms_direct_control_tests
    "${ARSTACK_ROOT}/tests/test_mms_static_direct_control.cpp")
target_link_libraries(ariec61850_mms_direct_control_tests PRIVATE
    ariec61850_mms_r12_server_core
    ARIEC61850::core)
target_compile_features(ariec61850_mms_direct_control_tests PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_direct_control_tests)

add_executable(ariec61850_mms_server_urcb_tests
    "${ARSTACK_ROOT}/tests/test_mms_server_urcb_report.cpp")
target_link_libraries(ariec61850_mms_server_urcb_tests PRIVATE
    ariec61850_mms_r12_server_core
    ARIEC61850::core)
target_compile_features(ariec61850_mms_server_urcb_tests PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_server_urcb_tests)

add_test(NAME mms_direct_control_unit COMMAND ariec61850_mms_direct_control_tests)
add_test(NAME mms_direct_control_help COMMAND ariec61850_mms_direct_control_server --help)
add_test(NAME mms_server_urcb_report COMMAND ariec61850_mms_server_urcb_tests)
add_test(NAME mms_urcb_server_help COMMAND ariec61850_mms_urcb_server --help)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME mms_server_live_discovery_loopback
    COMMAND
        ${Python3_EXECUTABLE}
        "${ARSTACK_ROOT}/scripts/test-mms-server-live-discovery.py"
        --client $<TARGET_FILE:ariec61850_mms_live_discover_client>
        --control-inventory $<TARGET_FILE:ariec61850_mms_control_inventory_client>
        --control-client $<TARGET_FILE:ariec61850_mms_control_interop_client>
        --direct-server $<TARGET_FILE:ariec61850_mms_direct_control_server>
        --urcb-server $<TARGET_FILE:ariec61850_mms_urcb_server>)
