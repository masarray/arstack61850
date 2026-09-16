cmake_minimum_required(VERSION 3.16)

# Keep the device identity version aligned with the Studio P0 release bundle.
# ESP-IDF exports this as the application version through esp_app_desc.
set(PROJECT_VER "0.1.1")

# Semantic firmware provenance. Explicit overrides are useful for controlled
# local builds. GitHub Actions uses the exact workflow commit, which is also
# written to the release manifest as sourceCommit. Local repository builds
# fall back to HEAD. Unknown provenance must never produce a flashable binary.
if(DEFINED ENV{ARSTACK_FIRMWARE_BUILD_ID} AND NOT "$ENV{ARSTACK_FIRMWARE_BUILD_ID}" STREQUAL "")
    set(ARSTACK_FIRMWARE_BUILD_ID "$ENV{ARSTACK_FIRMWARE_BUILD_ID}")
elseif(DEFINED ENV{GITHUB_SHA} AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 16 ARSTACK_FIRMWARE_BUILD_ID)
else()
    execute_process(
        COMMAND git rev-parse --short=16 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
        OUTPUT_VARIABLE ARSTACK_FIRMWARE_BUILD_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE ARSTACK_FIRMWARE_BUILD_ID_RESULT)
    if(NOT ARSTACK_FIRMWARE_BUILD_ID_RESULT EQUAL 0)
        message(FATAL_ERROR "Unable to establish semantic firmware build provenance")
    endif()
endif()
string(TOLOWER "${ARSTACK_FIRMWARE_BUILD_ID}" ARSTACK_FIRMWARE_BUILD_ID)
string(LENGTH "${ARSTACK_FIRMWARE_BUILD_ID}" ARSTACK_FIRMWARE_BUILD_ID_LENGTH)
if(NOT ARSTACK_FIRMWARE_BUILD_ID_LENGTH EQUAL 16 OR NOT ARSTACK_FIRMWARE_BUILD_ID MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "ARSTACK_FIRMWARE_BUILD_ID must be exactly 16 hexadecimal characters")
endif()
set(ARSTACK_FIRMWARE_BUILD_ID "${ARSTACK_FIRMWARE_BUILD_ID}" CACHE INTERNAL "ARStack semantic firmware build ID")
message(STATUS "ARStack semantic firmware build ID: ${ARSTACK_FIRMWARE_BUILD_ID}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(arstack_esp32p4_smv_injector)