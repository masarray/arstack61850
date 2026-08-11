cmake_minimum_required(VERSION 3.20)

project(ARStack61850MmsDirectControlServer LANGUAGES CXX)

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

set(SERVER_REPORT_SOURCES
    "${ARSTACK_ROOT}/src/mms/information_report_span.cpp"
    "${ARSTACK_ROOT}/src/mms/static_information_report.cpp"
    "${ARSTACK_ROOT}/src/mms/static_report_connection.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_objects.cpp"
    "${ARSTACK_ROOT}/src/mms/static_urcb_runtime.cpp")

add_executable(ariec61850_mms_direct_control_server
    "${ARSTACK_ROOT}/tools/mms_direct_control_server.cpp"
    "${ARSTACK_ROOT}/src/mms/static_direct_control.cpp")
target_include_directories(ariec61850_mms_direct_control_server PRIVATE
    "${ARSTACK_ROOT}/include")
target_link_libraries(ariec61850_mms_direct_control_server PRIVATE
    ARIEC61850::mms_server_core
    ARIEC61850::core)
if(WIN32)
    target_link_libraries(ariec61850_mms_direct_control_server PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_mms_direct_control_server PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_direct_control_server)

add_executable(ariec61850_mms_urcb_server
    "${ARSTACK_ROOT}/tools/mms_urcb_server.cpp"
    ${SERVER_REPORT_SOURCES})
target_include_directories(ariec61850_mms_urcb_server PRIVATE
    "${ARSTACK_ROOT}/include")
target_link_libraries(ariec61850_mms_urcb_server PRIVATE
    ARIEC61850::mms_server_core
    ARIEC61850::core)
if(WIN32)
    target_link_libraries(ariec61850_mms_urcb_server PRIVATE ws2_32)
endif()
target_compile_features(ariec61850_mms_urcb_server PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_urcb_server)

include(CTest)
enable_testing()

add_executable(ariec61850_mms_direct_control_tests
    "${ARSTACK_ROOT}/tests/test_mms_static_direct_control.cpp"
    "${ARSTACK_ROOT}/src/mms/static_direct_control.cpp")
target_include_directories(ariec61850_mms_direct_control_tests PRIVATE
    "${ARSTACK_ROOT}/include")
target_link_libraries(ariec61850_mms_direct_control_tests PRIVATE
    ARIEC61850::mms_server_core
    ARIEC61850::core)
target_compile_features(ariec61850_mms_direct_control_tests PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_direct_control_tests)

add_executable(ariec61850_mms_server_urcb_tests
    "${ARSTACK_ROOT}/tests/test_mms_server_urcb_report.cpp"
    ${SERVER_REPORT_SOURCES})
target_include_directories(ariec61850_mms_server_urcb_tests PRIVATE
    "${ARSTACK_ROOT}/include")
target_link_libraries(ariec61850_mms_server_urcb_tests PRIVATE
    ARIEC61850::mms_server_core
    ARIEC61850::core)
target_compile_features(ariec61850_mms_server_urcb_tests PRIVATE cxx_std_20)
server_strict_warnings(ariec61850_mms_server_urcb_tests)

add_test(NAME mms_direct_control_unit COMMAND ariec61850_mms_direct_control_tests)
add_test(NAME mms_direct_control_help COMMAND ariec61850_mms_direct_control_server --help)
add_test(NAME mms_server_urcb_report COMMAND ariec61850_mms_server_urcb_tests)
add_test(NAME mms_urcb_server_help COMMAND ariec61850_mms_urcb_server --help)
