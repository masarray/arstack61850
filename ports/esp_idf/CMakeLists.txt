# arstack61850 ESP-IDF component entry point.
#
# Add this directory to EXTRA_COMPONENT_DIRS from an ESP-IDF application. The
# component intentionally compiles only the MCU portability boundary plus the
# ESP-IDF Ethernet adapter; host discovery, filesystem, PCAP, COMTRADE and CLI
# sources are not pulled into firmware.

set(ARIEC61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(ARIEC61850_ESP_IDF_SOURCES
    ${ARIEC61850_ROOT}/src/asn1/ber.cpp
    ${ARIEC61850_ROOT}/src/ethernet/ethernet.cpp

    ${ARIEC61850_ROOT}/src/goose/retransmission_schedule.cpp
    ${ARIEC61850_ROOT}/src/goose/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/goose/frame_codec.cpp

    ${ARIEC61850_ROOT}/src/sampled_values/pdu_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/frame_codec.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/quality.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/payload_inspector.cpp
    ${ARIEC61850_ROOT}/src/sampled_values/sample_counter.cpp

    ${ARIEC61850_ROOT}/src/osi/tpkt.cpp
    ${ARIEC61850_ROOT}/src/osi/cotp.cpp
    ${ARIEC61850_ROOT}/src/osi/session.cpp
    ${ARIEC61850_ROOT}/src/osi/presentation.cpp
    ${ARIEC61850_ROOT}/src/acse/association.cpp

    ${ARIEC61850_ROOT}/src/mms/utc_time.cpp
    ${ARIEC61850_ROOT}/src/mms/data_value.cpp
    ${ARIEC61850_ROOT}/src/mms/data_codec.cpp
    ${ARIEC61850_ROOT}/src/mms/pdu.cpp
    ${ARIEC61850_ROOT}/src/mms/services.cpp

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

# RTTI is not required by the embedded protocol boundary. Exceptions remain
# enabled temporarily because legacy BER/MMS configuration/error paths still
# contain throws; the 4 kHz SV publish path itself is noexcept.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -fno-rtti)
