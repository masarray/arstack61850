cmake_minimum_required(VERSION 3.20)

project(ARIEC61850ControlHardProfile
    VERSION 0.1.0
    DESCRIPTION "No-exception guarded IEC 61850 control model profile"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(MSVC)
    message(FATAL_ERROR "Control hard profile is validated with GCC/Clang -fno-exceptions.")
endif()

add_executable(ariec61850_hard_guarded_control_smoke
    ${CMAKE_CURRENT_LIST_DIR}/../control_guarded_hard_profile_smoke.cpp
    ${ARIEC61850_ROOT}/src/control/guarded_control.cpp)

target_compile_features(ariec61850_hard_guarded_control_smoke PRIVATE cxx_std_20)
target_include_directories(ariec61850_hard_guarded_control_smoke PRIVATE
    ${ARIEC61850_ROOT}/include)
target_compile_definitions(ariec61850_hard_guarded_control_smoke PRIVATE
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0
    ARIEC61850_NO_EXCEPTIONS=1)
target_compile_options(ariec61850_hard_guarded_control_smoke PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Werror
    -fno-exceptions
    -fno-rtti)

enable_testing()
add_test(
    NAME ariec61850_hard_guarded_control_smoke
    COMMAND ariec61850_hard_guarded_control_smoke)

message(STATUS "arstack61850 Control hard profile: ctlModel Direct/SBO semantics=ON")
message(STATUS "arstack61850 Control hard profile: immutable selection sequence=ON")
message(STATUS "arstack61850 Control hard profile: enhanced CommandTermination boundary=ON")
message(STATUS "arstack61850 Control hard profile: authorization default-deny=ON")
message(STATUS "arstack61850 Control hard profile: automatic command retry=OFF")
message(STATUS "arstack61850 Control hard profile: exceptions/RTTI disabled=ON")
