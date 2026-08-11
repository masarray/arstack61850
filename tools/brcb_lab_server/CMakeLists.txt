cmake_minimum_required(VERSION 3.20)

project(ARIEC61850BrcbLabServer
    VERSION 0.1.0
    DESCRIPTION "Dedicated desktop BRCB interoperability lab server"
    LANGUAGES CXX)

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
set(ARIEC61850_WARNINGS_AS_ERRORS ON CACHE BOOL "" FORCE)

add_subdirectory(
    "${ARIEC61850_ROOT}"
    "${CMAKE_CURRENT_BINARY_DIR}/arstack-core")

add_executable(ariec61850_brcb_lab_server
    ${CMAKE_CURRENT_LIST_DIR}/../brcb_lab_server.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../static_ied_server_sigpipe.cpp)

target_link_libraries(
    ariec61850_brcb_lab_server
    PRIVATE ARIEC61850::mms_server_core)
target_compile_features(ariec61850_brcb_lab_server PRIVATE cxx_std_20)

if(WIN32)
    target_link_libraries(ariec61850_brcb_lab_server PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(ariec61850_brcb_lab_server PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(ariec61850_brcb_lab_server PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

enable_testing()
add_test(
    NAME ariec61850_brcb_lab_server_self_test
    COMMAND ariec61850_brcb_lab_server --self-test)
add_test(
    NAME ariec61850_brcb_lab_server_help
    COMMAND ariec61850_brcb_lab_server --help)
