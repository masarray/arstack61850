cmake_minimum_required(VERSION 3.20)

project(ARIEC61850ControlInteropProbe
    VERSION 0.1.0
    DESCRIPTION "Guarded IEC 61850 live control interoperability harness"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)

add_subdirectory(${ARIEC61850_ROOT} ariec61850-core)

add_executable(ariec61850_control_interop_probe
    ${ARIEC61850_ROOT}/tools/control_interop_probe.cpp)
target_link_libraries(ariec61850_control_interop_probe PRIVATE ARIEC61850::core)
target_compile_features(ariec61850_control_interop_probe PRIVATE cxx_std_20)

add_executable(ariec61850_control_inventory_probe
    ${ARIEC61850_ROOT}/tools/control_inventory_probe.cpp)
target_link_libraries(ariec61850_control_inventory_probe PRIVATE ARIEC61850::core)
target_compile_features(ariec61850_control_inventory_probe PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(ariec61850_control_interop_probe PRIVATE /W4 /WX /permissive-)
    target_compile_options(ariec61850_control_inventory_probe PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(ariec61850_control_interop_probe PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    target_compile_options(ariec61850_control_inventory_probe PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

enable_testing()
add_test(
    NAME ariec61850_control_interop_self_test
    COMMAND ariec61850_control_interop_probe --self-test)
add_test(
    NAME ariec61850_control_interop_help
    COMMAND ariec61850_control_interop_probe --help)
add_test(
    NAME ariec61850_control_inventory_self_test
    COMMAND ariec61850_control_inventory_probe --self-test)
add_test(
    NAME ariec61850_control_inventory_help
    COMMAND ariec61850_control_inventory_probe --help)

message(STATUS "arstack61850 C5 interop harness: read-only default=ON")
message(STATUS "arstack61850 C5 interop harness: explicit arm token required for Write=ON")
message(STATUS "arstack61850 C5 interop harness: automatic command retry=OFF")
message(STATUS "arstack61850 C5 control inventory companion: read-only only=ON")
