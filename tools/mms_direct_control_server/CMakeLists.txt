cmake_minimum_required(VERSION 3.20)

project(ARStack61850MmsR12ServerProfile LANGUAGES CXX)

get_filename_component(ARSTACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_WARNINGS_AS_ERRORS ON CACHE BOOL "" FORCE)

add_subdirectory("${ARSTACK_ROOT}" arstack-core)

function(server_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endfunction()

# Current-lineage R1/R2 overlay. These files are intentionally compiled once in
# one library while the parent #51 build graph remains the source of truth for
# ACSE/MMS transport, negotiated limits, RejectPDU behavior and BRCB runtime.
# Once this profile is green on GCC/Clang/MSVC the same five source entries can
# be promoted into the root ARIEC61850::mms_server_core without source drift.
add_library(ariec61850_mms_r12_overlay STATIC
    "${ARSTACK_ROOT}/src/mms/static_direct_control.cpp"
    "${ARSTACK_ROOT}/src/mms/static_information_report.cpp"
    "${ARSTACK_ROOT}/src/mms/static_report_connection.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_objects.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_runtime.cpp")
target_include_directories(ariec61850_mms_r12_overlay PUBLIC
    "${ARSTACK_ROOT}/include")
target_link_libraries(ariec61850_mms_r12_overlay PUBLIC
    ARIEC61850::mms_server_core
    ARIEC61850::core)
target_compile_features(ariec61850_mms_r12_overlay PUBLIC cxx_std_20)
server_strict_warnings(ariec61850_mms_r12_overlay)

add_executable(ariec61850_mms_direct_control_server
    "${ARSTACK_ROOT}/tools/mms_direct_control_server.cpp")
target_link_libraries(ariec61850_mms_direct_control_server PRIVATE
    ariec61850_mms_r12_overlay)
if(WIN32)
    target_link_libraries(ariec61850_mms_direct_control_server PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_mms_direct_control_server PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_direct_control_server)

add_executable(ariec61850_mms_urcb_server
    "${ARSTACK_ROOT}/tools/mms_urcb_server.cpp")
target_link_libraries(ariec61850_mms_urcb_server PRIVATE
    ariec61850_mms_r12_overlay)
if(WIN32)
    target_link_libraries(ariec61850_mms_urcb_server PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_mms_urcb_server PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_urcb_server)

include(CTest)
enable_testing()

add_executable(ariec61850_mms_direct_control_tests
    "${ARSTACK_ROOT}/tests/test_mms_static_direct_control.cpp")
target_link_libraries(ariec61850_mms_direct_control_tests PRIVATE
    ariec61850_mms_r12_overlay)
target_compile_features(ariec61850_mms_direct_control_tests PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_direct_control_tests)

add_executable(ariec61850_mms_server_urcb_tests
    "${ARSTACK_ROOT}/tests/test_mms_server_urcb_report.cpp")
target_link_libraries(ariec61850_mms_server_urcb_tests PRIVATE
    ariec61850_mms_r12_overlay)
target_compile_features(ariec61850_mms_server_urcb_tests PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_server_urcb_tests)

add_test(NAME mms_direct_control_unit COMMAND ariec61850_mms_direct_control_tests)
add_test(NAME mms_direct_control_help COMMAND ariec61850_mms_direct_control_server --help)
add_test(NAME mms_server_urcb_report COMMAND ariec61850_mms_server_urcb_tests)
add_test(NAME mms_urcb_server_help COMMAND ariec61850_mms_urcb_server --help)
