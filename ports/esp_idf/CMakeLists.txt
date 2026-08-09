# arstack61850 ESP-IDF component entry point.
#
# Add this directory to EXTRA_COMPONENT_DIRS from an ESP-IDF application.
# The MCU component is deliberately narrower than the desktop/host core: it
# contains only the bounded dependencies required to build and transmit Sampled
# Values Ethernet frames plus the ESP-IDF raw-Ethernet adapter. Host discovery,
# dynamic BER decode, generic Ethernet object codecs, MMS-over-TCP runtime,
# reporting, GOOSE, SCL, PCAP, COMTRADE, filesystem and CLI sources stay outside
# this first target-specific link boundary.

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(ARIEC61850_ESP_IDF_SOURCES
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
    ARIEC61850_HOST_SERVICES=0
    ARIEC61850_NO_EXCEPTIONS=1)

# The physical SV transmit slice is now a hard embedded profile: no C++
# exceptions and no RTTI. Host convenience/decode APIs are excluded at compile
# time; the linked wire path uses status-returning encode_into() operations and
# caller-owned frame storage.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    -fno-exceptions -fno-rtti)

# Keep -Wpedantic on the portable protocol sources. The one ESP-IDF adapter must
# include vendor C headers which intentionally use GNU extensions such as
# #include_next and anonymous structs. Suppress only pedantic diagnostics for this
# translation unit; ESP-IDF's global -Werror and all other arstack warnings remain
# active. This prevents vendor-header syntax from weakening the core warning gate.
set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/ethernet_port.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-pedantic")
