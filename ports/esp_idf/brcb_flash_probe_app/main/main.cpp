// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ports/esp_idf/brcb_flash_storage.hpp"

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {

using ar::iec61850::ports::esp_idf::BrcbFlashStorage;

std::array<std::uint8_t, 3072U> g_probe_payload{};

} // namespace

extern "C" void app_main(void) {
    using namespace ar::iec61850::ports::esp_idf;

    BrcbFlashStorage storage;
    const auto status = storage.initialize();
    const auto& geometry = storage.geometry();

    std::printf(
        "ARBRCB_FLASH {\"stage\":\"geometry\",\"status\":\"%s\","
        "\"partitionBytes\":%u,\"eraseBytes\":%u,\"bankBytes\":%u,"
        "\"journalBytes\":%u,\"probeOffset\":%u,\"probeBytes\":%u,"
        "\"maximumStateBytes\":%u,\"eraseUnitsPerCheckpoint\":%u,"
        "\"unusedBytes\":%u,\"encrypted\":%s,\"readOnly\":%s}\n",
        brcb_flash_status_name(status),
        static_cast<unsigned>(geometry.partition_bytes),
        static_cast<unsigned>(geometry.erase_bytes),
        static_cast<unsigned>(geometry.bank_bytes),
        static_cast<unsigned>(geometry.journal_bytes),
        static_cast<unsigned>(geometry.probe_offset),
        static_cast<unsigned>(geometry.probe_bytes),
        static_cast<unsigned>(geometry.maximum_state_bytes),
        static_cast<unsigned>(geometry.erase_units_per_checkpoint),
        static_cast<unsigned>(geometry.unused_bytes),
        geometry.encrypted ? "true" : "false",
        geometry.read_only ? "true" : "false");
    std::fflush(stdout);

    if (!storage.ready()) {
        return;
    }

    for (std::size_t index = 0U; index < g_probe_payload.size(); ++index) {
        g_probe_payload[index] = static_cast<std::uint8_t>(
            (index * 37U + 0x5AU) & 0xFFU);
    }

    const auto result = storage.run_latency_probe(
        std::span<const std::uint8_t>{g_probe_payload});
    std::printf(
        "ARBRCB_FLASH {\"stage\":\"latency-probe\",\"status\":\"%s\","
        "\"payloadBytes\":%u,\"prepareEraseUs\":%" PRIu64 ","
        "\"headerWriteUs\":%" PRIu64 ",\"payloadWriteUs\":%" PRIu64 ","
        "\"footerWriteUs\":%" PRIu64 ",\"totalWriteUs\":%" PRIu64 ","
        "\"verifyReadUs\":%" PRIu64 ",\"cleanupEraseUs\":%" PRIu64 ","
        "\"probeEraseUnits\":%" PRIu64 ",\"cleanupSucceeded\":%s}\n",
        brcb_flash_probe_status_name(result.status),
        static_cast<unsigned>(result.payload_bytes),
        result.prepare_erase_us,
        result.header_write_us,
        result.payload_write_us,
        result.footer_write_us,
        result.total_write_us(),
        result.verify_read_us,
        result.cleanup_erase_us,
        result.probe_erase_units,
        result.cleanup_succeeded ? "true" : "false");
    std::fflush(stdout);
}
