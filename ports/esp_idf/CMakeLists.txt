# arstack61850 ESP-IDF component entry point.
#
# Add this directory to EXTRA_COMPONENT_DIRS from an ESP-IDF application.
# This first MCU component is deliberately narrower than the desktop/host core:
# it contains only the dependencies required to build and transmit Sampled
# Values Ethernet frames plus the ESP-IDF raw-Ethernet adapter. Host discovery,
# MMS-over-TCP runtime, reporting, GOOSE, SCL, PCAP, COMTRADE, filesystem and CLI
# sources remain outside the first hardware proof.

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(ARIEC61850_ESP_IDF_SOURCES
    ${ARIEC61850_ROOT}/src/asn1/ber.cpp
    ${ARIEC61850_ROOT}/src/ethernet/ethernet.cpp

    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp

    ${ARIEC61850_ROOT}/src/sampled_values/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/frame_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/sample_counter.cpp

    ${CMAKE_CURRENT_LIST_DIR}/ethernet_port.cpp)

idf_component_register(
    SRCS ${ARIEC61850_ESP_IDF_SOURCES}
    INCLUDE_DIRS
        ${ARIEC61850_ROOT}/include
        ${CMAKE_CURRENT_LIST_DIR}/include
    REQUIRES esp_eth esp_timer)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0)

# RTTI is not required by this process-bus transmit slice.
#
# IMPORTANT: the shared BER/Ethernet/SV source files still retain exception-based
# convenience/validation APIs used by the desktop stack. ESP-IDF disables C++
# exceptions by default, so an application using this transitional component must
# enable CONFIG_COMPILER_CXX_EXCEPTIONS. The steady-state publisher API itself is
# noexcept and uses status-returning encode_into()/RawEthernetPort operations.
# Removing the remaining compile-time exception dependency is a separate embedded
# hardening gate; do not hide it by weakening the cross-compile CI.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -fno-rtti)
