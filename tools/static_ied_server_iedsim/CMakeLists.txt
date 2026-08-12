cmake_minimum_required(VERSION 3.20)

project(ARStack61850IedScoutParityServer LANGUAGES CXX)

get_filename_component(ARSTACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_WARNINGS_AS_ERRORS ON CACHE BOOL "" FORCE)

# Keep the regular client library available, but build the desktop server with
# the C#-simulator parity runtime instead of the strict embedded server runtime.
add_subdirectory("${ARSTACK_ROOT}" arstack-core)

function(parity_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endfunction()

add_library(ariec61850_iedscout_parity_server_core STATIC
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
    "${ARSTACK_ROOT}/src/mms/static_information_report.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_runtime.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_objects.cpp"
    "${ARSTACK_ROOT}/src/mms/static_report_connection.cpp"
    "${ARSTACK_ROOT}/src/mms/buffered_selective_information_report_span.cpp"
    "${ARSTACK_ROOT}/src/mms/static_brcb_runtime.cpp"
    "${ARSTACK_ROOT}/src/mms/static_brcb_control.cpp"
    "${ARSTACK_ROOT}/src/mms/static_brcb_objects.cpp"
    "${ARSTACK_ROOT}/src/mms/static_brcb_connection.cpp"
    "${ARSTACK_ROOT}/src/mms/static_object_table.cpp"
    "${ARSTACK_ROOT}/src/mms/static_write.cpp"
    "${ARSTACK_ROOT}/src/mms/static_data_set_table.cpp"
    "${ARSTACK_ROOT}/src/mms/static_dispatcher.cpp"
    "${ARSTACK_ROOT}/src/mms/connection_runtime_iedsim.cpp"
    "${ARSTACK_ROOT}/src/mms/static_server_session.cpp")
target_include_directories(ariec61850_iedscout_parity_server_core PUBLIC
    "${ARSTACK_ROOT}/include")
target_compile_features(ariec61850_iedscout_parity_server_core PUBLIC cxx_std_20)
parity_strict_warnings(ariec61850_iedscout_parity_server_core)

add_executable(ariec61850_static_ied_server_iedsim
    "${ARSTACK_ROOT}/tools/static_ied_server.cpp"
    "${ARSTACK_ROOT}/tools/static_ied_server_sigpipe.cpp")
target_link_libraries(ariec61850_static_ied_server_iedsim PRIVATE
    ariec61850_iedscout_parity_server_core
    ARIEC61850::core)
if(WIN32)
    target_link_libraries(ariec61850_static_ied_server_iedsim PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_static_ied_server_iedsim PRIVATE cxx_std_20)
parity_strict_warnings(ariec61850_static_ied_server_iedsim)

add_executable(ariec61850_iedscout_parity_live_discover
    "${ARSTACK_ROOT}/tools/live_discover.cpp")
target_link_libraries(ariec61850_iedscout_parity_live_discover PRIVATE
    ARIEC61850::core)
target_compile_features(ariec61850_iedscout_parity_live_discover PRIVATE cxx_std_20)
parity_strict_warnings(ariec61850_iedscout_parity_live_discover)

add_executable(ariec61850_iedscout_parity_read_probe
    "${ARSTACK_ROOT}/tools/mms_read_probe.cpp")
target_link_libraries(ariec61850_iedscout_parity_read_probe PRIVATE
    ARIEC61850::core)
target_compile_features(ariec61850_iedscout_parity_read_probe PRIVATE cxx_std_20)
parity_strict_warnings(ariec61850_iedscout_parity_read_probe)

enable_testing()
add_test(NAME iedscout_parity_server_help
    COMMAND ariec61850_static_ied_server_iedsim --help)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME iedscout_parity_server_loopback
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_LIST_DIR}/test_loopback.py"
        --server $<TARGET_FILE:ariec61850_static_ied_server_iedsim>
        --client $<TARGET_FILE:ariec61850_iedscout_parity_live_discover>)

add_test(
    NAME iedscout_parity_runtime_model
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_LIST_DIR}/test_runtime_model.py"
        --server $<TARGET_FILE:ariec61850_static_ied_server_iedsim>
        --discovery $<TARGET_FILE:ariec61850_iedscout_parity_live_discover>
        --read-probe $<TARGET_FILE:ariec61850_iedscout_parity_read_probe>)
