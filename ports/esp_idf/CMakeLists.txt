# arstack61850 ESP-IDF component entry point.
#
# Add this directory to EXTRA_COMPONENT_DIRS from an ESP-IDF application.
# The MCU component remains deliberately narrower than the desktop/host core: it
# contains the bounded dependencies required to build/transmit Sampled Values,
# the ESP-IDF raw-Ethernet adapter, and the raw-partition adapter used by the
# storage-agnostic BRCB checkpoint journal. Host discovery, dynamic BER decode,
# generic Ethernet object codecs, MMS-over-TCP runtime, filesystem and CLI
# sources stay outside this target-specific link boundary.

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(ARIEC61850_ESP_IDF_SOURCES
    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp

    ${ARIEC61850_ROOT}/src/sampled_values/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/frame_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/sample_counter.cpp

    ${CMAKE_CURRENT_LIST_DIR}/ethernet_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/brcb_flash_storage.cpp)

idf_component_register(
    SRCS ${ARIEC61850_ESP_IDF_SOURCES}
    INCLUDE_DIRS
        ${ARIEC61850_ROOT}/include
        ${CMAKE_CURRENT_LIST_DIR}/include
    REQUIRES esp_eth esp_timer esp_partition)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ARIEC61850_EMBEDDED_PROFILE=1
    ARIEC61850_HOST_SERVICES=0
    ARIEC61850_NO_EXCEPTIONS=1)

# The physical transmit/storage adapters remain part of the hard embedded
# profile: no C++ exceptions and no RTTI. Host convenience/decode APIs are
# excluded at compile time and all protocol/storage paths use status returns and
# caller-owned buffers.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    -fno-exceptions -fno-rtti)

# ESP-IDF vendor C headers intentionally use GNU extensions such as #include_next
# and anonymous structs. Suppress only pedantic diagnostics for the platform
# adapter translation units; ESP-IDF's global -Werror and every other arstack
# warning remain active.
set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/ethernet_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/brcb_flash_storage.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-pedantic")
