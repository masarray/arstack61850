cmake_minimum_required(VERSION 3.20)

project(ARStack61850PtpProfile LANGUAGES CXX)

get_filename_component(ARSTACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(ptp_profile_core STATIC
    "${ARSTACK_ROOT}/src/ethernet/ethernet.cpp"
    "${ARSTACK_ROOT}/src/time_sync/ptp.cpp"
    "${ARSTACK_ROOT}/src/time_sync/ptp_monitor.cpp"
    "${ARSTACK_ROOT}/src/capture/pcap.cpp")

target_include_directories(ptp_profile_core PUBLIC
    "${ARSTACK_ROOT}/include")
target_compile_features(ptp_profile_core PUBLIC cxx_std_20)

add_executable(ptp_profile_tests
    "${ARSTACK_ROOT}/tests/test_ptp.cpp")
target_link_libraries(ptp_profile_tests PRIVATE ptp_profile_core)
target_compile_features(ptp_profile_tests PRIVATE cxx_std_20)

add_executable(ptp_runtime_tests
    "${ARSTACK_ROOT}/tests/test_ptp_runtime.cpp")
target_link_libraries(ptp_runtime_tests PRIVATE ptp_profile_core)
target_compile_features(ptp_runtime_tests PRIVATE cxx_std_20)

add_executable(smp_synch_policy_tests
    "${ARSTACK_ROOT}/tests/test_smp_synch_policy.cpp")
target_link_libraries(smp_synch_policy_tests PRIVATE ptp_profile_core)
target_compile_features(smp_synch_policy_tests PRIVATE cxx_std_20)

add_executable(smp_synch_wire_offset_tests
    "${ARSTACK_ROOT}/tests/test_smp_synch_wire_offset.cpp")
target_include_directories(smp_synch_wire_offset_tests PRIVATE "${ARSTACK_ROOT}/include")
target_compile_features(smp_synch_wire_offset_tests PRIVATE cxx_std_20)

add_executable(ptp_discipline_tests
    "${ARSTACK_ROOT}/tests/test_ptp_discipline.cpp")
target_link_libraries(ptp_discipline_tests PRIVATE ptp_profile_core)
target_compile_features(ptp_discipline_tests PRIVATE cxx_std_20)

add_executable(ptp_profile_analyze
    "${ARSTACK_ROOT}/tools/ptp_analyze.cpp")
target_link_libraries(ptp_profile_analyze PRIVATE ptp_profile_core)
target_compile_features(ptp_profile_analyze PRIVATE cxx_std_20)

foreach(target IN ITEMS ptp_profile_core ptp_profile_tests ptp_runtime_tests smp_synch_policy_tests smp_synch_wire_offset_tests ptp_discipline_tests ptp_profile_analyze)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endforeach()

enable_testing()
add_test(NAME ptp_profile_tests COMMAND ptp_profile_tests)
add_test(NAME ptp_runtime_tests COMMAND ptp_runtime_tests)
add_test(NAME smp_synch_policy_tests COMMAND smp_synch_policy_tests)
add_test(NAME smp_synch_wire_offset_tests COMMAND smp_synch_wire_offset_tests)
add_test(NAME ptp_discipline_tests COMMAND ptp_discipline_tests)
add_test(NAME ptp_profile_analyze_help COMMAND ptp_profile_analyze --help)
