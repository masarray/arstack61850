// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/embedded/profile.hpp"
#include "ariec61850/ports/esp_idf/brcb_flash_storage.hpp"
#include "ariec61850/ports/esp_idf/ethernet_port.hpp"
#include "ariec61850/sampled_values/payload_writer.hpp"
#include "ariec61850/sampled_values/publisher.hpp"

#include <array>
#include <cstdint>
#include <span>

extern "C" void app_main(void) {
    using namespace ar::iec61850;

    static_assert(embedded::Esp32SmallProfile::ethernet_frame_bytes >= 1'522U);
    static_assert(sampled_values::SampledValuesPublisher::maximum_supported_sample_rate_hz >= 4'000U);
    static_assert(sampled_values::SampledValuesPayloadWriter::int32_quality_pair_bytes == 8U);
    static_assert(ports::esp_idf::BrcbFlashStorage::maximum_bank_erase_cycles(3U) == 2U);
    static_assert(ports::esp_idf::BrcbFlashStorage::checkpoint_attempts_for_bank_cycles(10U) == 20U);

    ports::esp_idf::RawEthernetContext context{};
    const auto port = ports::esp_idf::make_raw_ethernet_port(context);
    const auto clock = ports::esp_idf::make_monotonic_clock();

    ports::esp_idf::BrcbFlashStorage flash_storage;
    const auto flash_status = flash_storage.initialize();
    const auto flash_backend = flash_storage.backend();
    const auto flash_store = flash_storage.checkpoint_store();

    std::array<std::uint8_t, 8U> payload{};
    const auto wrote = sampled_values::SampledValuesPayloadWriter::write_int32_quality_pair(
        std::span<std::uint8_t>{payload}, 0U, 1234, 0U);

    // Build-only smoke: no Ethernet/flash hardware is exercised on hosted CI.
    // Keep the objects live so adapter symbols, partition APIs and C++20 span
    // usage are compiled and linked for the real ESP32-P4 toolchain.
    (void)port;
    (void)clock;
    (void)flash_status;
    (void)flash_backend;
    (void)flash_store;
    (void)wrote;
}
