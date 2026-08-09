cmake_minimum_required(VERSION 3.20)

project(ARIEC61850HardSvProfile
    VERSION 0.1.0
    DESCRIPTION "No-exception/no-RTTI MCU profile for bounded process-bus and SV transmit paths"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(MSVC)
    message(FATAL_ERROR "The first hard embedded profile is validated with GCC/Clang -fno-exceptions.")
endif()

set(ARIEC61850_HARD_SV_SOURCES
    ${ARIEC61850_ROOT}/src/ethernet/ethernet.cpp
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/frame_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/sample_counter.cpp)

add_executable(ariec61850_hard_sv_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../sv_hard_profile_smoke.cpp
    ${ARIEC61850_HARD_SV_SOURCES})

target_compile_features(ariec61850_hard_sv_smoke PRIVATE cxx_std_20)
target_include_directories(ariec61850_hard_sv_smoke PRIVATE ${ARIEC61850_ROOT}/include)
target_compile_definitions(ariec61850_hard_sv_smoke PRIVATE
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0
    ARIEC61850_NO_EXCEPTIONS=1)
target_compile_options(ariec61850_hard_sv_smoke PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Werror
    -fno-exceptions
    -fno-rtti)

enable_testing()
add_test(NAME ariec61850_hard_sv_smoke COMMAND ariec61850_hard_sv_smoke)

message(STATUS "arstack61850 hard process-bus/SV profile: exceptions disabled=ON")
message(STATUS "arstack61850 hard process-bus/SV profile: RTTI disabled=ON")
message(STATUS "arstack61850 hard process-bus/SV profile: host services excluded")
