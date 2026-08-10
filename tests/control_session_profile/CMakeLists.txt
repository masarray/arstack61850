cmake_minimum_required(VERSION 3.20)

project(ARIEC61850ControlSessionProfile
    VERSION 0.1.0
    DESCRIPTION "IEC 61850 smart-control discovery and session parity validation"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(ariec61850_control_strict target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${ARIEC61850_ROOT}/include)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
endfunction()

add_executable(ariec61850_control_session_tests
    ${ARIEC61850_ROOT}/tests/test_control_session.cpp
    ${ARIEC61850_ROOT}/src/asn1/ber.cpp
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/mms/data_value.cpp
    ${ARIEC61850_ROOT}/src/mms/data_codec.cpp
    ${ARIEC61850_ROOT}/src/mms/services.cpp
    ${ARIEC61850_ROOT}/src/control/guarded_control.cpp
    ${ARIEC61850_ROOT}/src/control/mms_control_structure.cpp
    ${ARIEC61850_ROOT}/src/control/command_termination.cpp
    ${ARIEC61850_ROOT}/src/control/control_discovery.cpp
    ${ARIEC61850_ROOT}/src/control/control_session.cpp)
ariec61850_control_strict(ariec61850_control_session_tests)

# Production adapter compile gate. Linking is exercised by repository C++ CI
# once these sources are part of ariec61850_core; this target catches API and
# warning regressions without duplicating the complete association link graph.
add_library(ariec61850_control_transport_compile OBJECT
    ${ARIEC61850_ROOT}/src/control/mms_association_control_transport.cpp
    ${ARIEC61850_ROOT}/src/mms/association_runtime_control_wait.cpp)
ariec61850_control_strict(ariec61850_control_transport_compile)

enable_testing()
add_test(
    NAME ariec61850_control_session_tests
    COMMAND ariec61850_control_session_tests)

message(STATUS "arstack61850 Control session: ctlModel + live GVAA discovery=ON")
message(STATUS "arstack61850 Control session: Direct/SBO/SBOw/Oper/Cancel orchestration=ON")
message(STATUS "arstack61850 Control session: subscribe/drain before command Write=ON")
message(STATUS "arstack61850 Control session: enhanced CommandTermination completion=ON")
message(STATUS "arstack61850 Control session: command retry=OFF")
message(STATUS "arstack61850 Control session: default authorization=DENY")
