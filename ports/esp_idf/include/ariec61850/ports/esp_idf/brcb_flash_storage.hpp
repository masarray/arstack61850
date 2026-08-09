// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_brcb_checkpoint_store.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

struct esp_partition_t;

namespace ar::iec61850::ports::esp_idf {

enum class BrcbFlashStatus : std::uint8_t {
    ok,
    partition_not_found,
    read_only_partition,
    encrypted_partition_not_supported,
    invalid_geometry,
    partition_too_small,
};

struct BrcbFlashGeometry final {
    std::size_t partition_bytes{};
    std::size_t erase_bytes{};
    std::size_t bank_bytes{};
    std::size_t journal_bytes{};
    std::size_t probe_offset{};
    std::size_t probe_bytes{};
    std::size_t unused_bytes{};
    std::size_t maximum_state_bytes{};
    std::size_t erase_units_per_checkpoint{};
    bool encrypted{};
    bool read_only{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return erase_bytes != 0U && bank_bytes == erase_bytes &&
            journal_bytes == bank_bytes * 2U && probe_bytes == erase_bytes &&
            probe_offset == journal_bytes && maximum_state_bytes != 0U &&
            erase_units_per_checkpoint == 1U;
    }
};

struct BrcbFlashMetrics final {
    std::uint64_t read_calls{};
    std::uint64_t write_calls{};
    std::uint64_t erase_calls{};
    std::uint64_t sync_calls{};
    std::uint64_t read_failures{};
    std::uint64_t write_failures{};
    std::uint64_t erase_failures{};
    std::uint64_t read_bytes{};
    std::uint64_t write_bytes{};
    std::uint64_t erased_bytes{};
    std::uint64_t read_us_total{};
    std::uint64_t write_us_total{};
    std::uint64_t erase_us_total{};
    std::uint64_t read_us_max{};
    std::uint64_t write_us_max{};
    std::uint64_t erase_us_max{};

    [[nodiscard]] constexpr std::uint64_t logical_erase_units(
        const std::size_t erase_bytes) const noexcept {
        return erase_bytes == 0U
            ? 0U
            : erased_bytes / static_cast<std::uint64_t>(erase_bytes);
    }
};

enum class BrcbFlashProbeStatus : std::uint8_t {
    ok,
    not_ready,
    payload_too_large,
    prepare_erase_failed,
    header_write_failed,
    payload_write_failed,
    footer_write_failed,
    header_read_failed,
    payload_read_failed,
    footer_read_failed,
    verify_failed,
    cleanup_erase_failed,
};

struct BrcbFlashProbeResult final {
    BrcbFlashProbeStatus status{BrcbFlashProbeStatus::not_ready};
    std::size_t payload_bytes{};
    std::uint64_t prepare_erase_us{};
    std::uint64_t header_write_us{};
    std::uint64_t payload_write_us{};
    std::uint64_t footer_write_us{};
    std::uint64_t verify_read_us{};
    std::uint64_t cleanup_erase_us{};
    std::uint64_t probe_erase_units{};
    bool cleanup_succeeded{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == BrcbFlashProbeStatus::ok;
    }

    [[nodiscard]] constexpr std::uint64_t total_write_us() const noexcept {
        return header_write_us + payload_write_us + footer_write_us;
    }
};

// ESP-IDF raw-partition adapter for the bounded BRCB A/B checkpoint journal.
//
// Layout policy:
//   erase unit 0 = journal bank A
//   erase unit 1 = journal bank B
//   erase unit 2 = destructive latency-probe scratch area
//
// Exactly one erase unit is therefore erased per checkpoint attempt that reaches
// the storage erase stage. The probe sector is deliberately outside the backend
// capacity exposed to MmsStaticBrcbCheckpointStore so a latency measurement can
// never overwrite either committed journal bank.
class BrcbFlashStorage final {
public:
    static constexpr std::uint8_t partition_type = 0x40U;
    static constexpr std::uint8_t partition_subtype = 0x01U;
    static constexpr std::size_t journal_bank_count = 2U;
    static constexpr std::size_t probe_sector_count = 1U;
    static constexpr const char* default_partition_label = "brcb_state";

    [[nodiscard]] BrcbFlashStatus initialize(
        const char* label = default_partition_label) noexcept;

    [[nodiscard]] constexpr bool ready() const noexcept {
        return status_ == BrcbFlashStatus::ok && partition_ != nullptr && geometry_.valid();
    }

    [[nodiscard]] constexpr BrcbFlashStatus status() const noexcept {
        return status_;
    }

    [[nodiscard]] constexpr const BrcbFlashGeometry& geometry() const noexcept {
        return geometry_;
    }

    [[nodiscard]] constexpr const BrcbFlashMetrics& metrics() const noexcept {
        return metrics_;
    }

    void reset_metrics() noexcept {
        metrics_ = {};
    }

    [[nodiscard]] mms::MmsStaticBrcbStorageBackend backend() noexcept;

    [[nodiscard]] mms::MmsStaticBrcbCheckpointStore checkpoint_store() noexcept {
        return mms::MmsStaticBrcbCheckpointStore{backend(), geometry_.bank_bytes};
    }

    // Destructive only inside the dedicated probe erase unit. The payload is
    // written using the same header/payload/footer write shape as the journal,
    // then read back and verified before the probe unit is erased again.
    [[nodiscard]] BrcbFlashProbeResult run_latency_probe(
        std::span<const std::uint8_t> payload) noexcept;

    // A/B alternation means each journal bank is erased at most every other
    // checkpoint erase attempt. These helpers intentionally avoid assuming any
    // vendor-specific flash endurance rating; feed the actual flash rating into
    // checkpoint_attempts_for_bank_cycles() when hardware data is available.
    [[nodiscard]] static constexpr std::uint64_t maximum_bank_erase_cycles(
        const std::uint64_t checkpoint_erase_attempts) noexcept {
        return (checkpoint_erase_attempts + 1U) / 2U;
    }

    [[nodiscard]] static constexpr std::uint64_t checkpoint_attempts_for_bank_cycles(
        const std::uint64_t rated_bank_erase_cycles) noexcept {
        return rated_bank_erase_cycles >
                (std::numeric_limits<std::uint64_t>::max() / 2U)
            ? std::numeric_limits<std::uint64_t>::max()
            : rated_bank_erase_cycles * 2U;
    }

private:
    [[nodiscard]] static bool storage_read(
        void* context,
        std::size_t offset,
        std::span<std::uint8_t> destination) noexcept;
    [[nodiscard]] static bool storage_write(
        void* context,
        std::size_t offset,
        std::span<const std::uint8_t> source) noexcept;
    [[nodiscard]] static bool storage_erase(
        void* context,
        std::size_t offset,
        std::size_t bytes) noexcept;
    [[nodiscard]] static bool storage_sync(void* context) noexcept;

    const struct esp_partition_t* partition_{};
    BrcbFlashStatus status_{BrcbFlashStatus::partition_not_found};
    BrcbFlashGeometry geometry_{};
    BrcbFlashMetrics metrics_{};
};

[[nodiscard]] const char* brcb_flash_status_name(BrcbFlashStatus status) noexcept;
[[nodiscard]] const char* brcb_flash_probe_status_name(BrcbFlashProbeStatus status) noexcept;

} // namespace ar::iec61850::ports::esp_idf
