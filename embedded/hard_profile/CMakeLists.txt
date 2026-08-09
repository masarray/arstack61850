cmake_minimum_required(VERSION 3.20)

project(ARIEC61850HardProcessBusProfile
    VERSION 0.1.0
    DESCRIPTION "No-exception/no-RTTI MCU profiles for bounded process-bus transmit paths"
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
    ${ARIEC61850_ROOT}/src/ethernet/ethernet.cpp
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/frame_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/sample_counter.cpp)
ariec61850_configure_hard_target(ariec61850_hard_sv_smoke)

add_executable(ariec61850_hard_goose_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../goose_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/ethernet/ethernet.cpp
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/mms/data_value.cpp
    ${ARIEC61850_ROOT}/src/goose/pdu_encode.cpp
    ${ARIEC61850_ROOT}/src/goose/frame_encode.cpp)
ariec61850_configure_hard_target(ariec61850_hard_goose_smoke)

enable_testing()
add_test(NAME ariec61850_hard_sv_smoke COMMAND ariec61850_hard_sv_smoke)
add_test(NAME ariec61850_hard_goose_smoke COMMAND ariec61850_hard_goose_smoke)

message(STATUS "arstack61850 hard process-bus profile: exceptions disabled=ON")
message(STATUS "arstack61850 hard process-bus profile: RTTI disabled=ON")
message(STATUS "arstack61850 hard process-bus profile: host services excluded")
message(STATUS "arstack61850 hard process-bus profile: bounded SV + GOOSE transmit gates=ON")
