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

add_executable(ptp_profile_analyze
    "${ARSTACK_ROOT}/tools/ptp_analyze.cpp")
target_link_libraries(ptp_profile_analyze PRIVATE ptp_profile_core)
target_compile_features(ptp_profile_analyze PRIVATE cxx_std_20)

foreach(target IN ITEMS ptp_profile_core ptp_profile_tests ptp_profile_analyze)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endforeach()

enable_testing()
add_test(NAME ptp_profile_tests COMMAND ptp_profile_tests)
add_test(NAME ptp_profile_analyze_help COMMAND ptp_profile_analyze --help)
