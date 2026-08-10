// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ports/esp_idf/brcb_flash_storage.hpp"

#include "esp_partition.h"
#include "esp_timer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::ports::esp_idf {
namespace {

[[nodiscard]] std::uint64_t timer_us() noexcept {
    const auto value = esp_timer_get_time();
    return value <= 0 ? 0U : static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint64_t elapsed_us(const std::uint64_t start) noexcept {
    const auto end = timer_us();
    return end >= start ? end - start : 0U;
}

[[nodiscard]] bool range_valid(
    const std::size_t offset,
    const std::size_t bytes,
    const std::size_t capacity) noexcept {
    return offset <= capacity && bytes <= capacity - offset;
}

void record_latency(
    std::uint64_t& total,
    std::uint64_t& maximum,
    const std::uint64_t elapsed) noexcept {
    total += elapsed;
    maximum = std::max(maximum, elapsed);
}

} // namespace

BrcbFlashStatus BrcbFlashStorage::initialize(const char* label) noexcept {
    partition_ = nullptr;
    geometry_ = {};
    metrics_ = {};
    status_ = BrcbFlashStatus::partition_not_found;

    if (label == nullptr) {
        return status_;
    }

    partition_ = esp_partition_find_first(
        static_cast<esp_partition_type_t>(partition_type),
        static_cast<esp_partition_subtype_t>(partition_subtype),
        label);
    if (partition_ == nullptr) {
        return status_;
    }

    geometry_.partition_bytes = static_cast<std::size_t>(partition_->size);
    geometry_.erase_bytes = static_cast<std::size_t>(partition_->erase_size);
    geometry_.encrypted = partition_->encrypted;
    geometry_.read_only = partition_->readonly;

    if (partition_->readonly) {
        status_ = BrcbFlashStatus::read_only_partition;
        return status_;
    }
    if (partition_->encrypted) {
        // The current journal emits byte-sized writes including a 40-byte header.
        // Keep this first physical backend fail-closed until encrypted-partition
        // alignment and power-fail behavior are characterized separately.
        status_ = BrcbFlashStatus::encrypted_partition_not_supported;
        return status_;
    }

    const auto erase_bytes = geometry_.erase_bytes;
    if (erase_bytes == 0U ||
        (static_cast<std::size_t>(partition_->address) % erase_bytes) != 0U ||
        (geometry_.partition_bytes % erase_bytes) != 0U) {
        status_ = BrcbFlashStatus::invalid_geometry;
        return status_;
    }

    constexpr std::size_t required_units = journal_bank_count + probe_sector_count;
    if (erase_bytes > (std::numeric_limits<std::size_t>::max() / required_units)) {
        status_ = BrcbFlashStatus::invalid_geometry;
        return status_;
    }
    const auto required_bytes = erase_bytes * required_units;
    if (geometry_.partition_bytes < required_bytes) {
        status_ = BrcbFlashStatus::partition_too_small;
        return status_;
    }
    if (erase_bytes <=
        mms::MmsStaticBrcbCheckpointStore::record_header_bytes +
            mms::MmsStaticBrcbCheckpointStore::record_footer_bytes) {
        status_ = BrcbFlashStatus::invalid_geometry;
        return status_;
    }

    geometry_.bank_bytes = erase_bytes;
    geometry_.journal_bytes = erase_bytes * journal_bank_count;
    geometry_.probe_offset = geometry_.journal_bytes;
    geometry_.probe_bytes = erase_bytes;
    geometry_.unused_bytes = geometry_.partition_bytes - required_bytes;
    geometry_.maximum_state_bytes =
        erase_bytes - mms::MmsStaticBrcbCheckpointStore::record_header_bytes -
        mms::MmsStaticBrcbCheckpointStore::record_footer_bytes;
    geometry_.erase_units_per_checkpoint = 1U;

    status_ = geometry_.valid()
        ? BrcbFlashStatus::ok
        : BrcbFlashStatus::invalid_geometry;
    return status_;
}

mms::MmsStaticBrcbStorageBackend BrcbFlashStorage::backend() noexcept {
    if (!ready()) {
        return {};
    }
    return mms::MmsStaticBrcbStorageBackend{
        this,
        geometry_.journal_bytes,
        &BrcbFlashStorage::storage_read,
        &BrcbFlashStorage::storage_write,
        &BrcbFlashStorage::storage_erase,
        &BrcbFlashStorage::storage_sync};
}

bool BrcbFlashStorage::storage_read(
    void* context,
    const std::size_t offset,
    const std::span<std::uint8_t> destination) noexcept {
    auto* storage = static_cast<BrcbFlashStorage*>(context);
    if (storage == nullptr || !storage->ready() ||
        !range_valid(offset, destination.size(), storage->geometry_.journal_bytes)) {
        return false;
    }
    ++storage->metrics_.read_calls;
    if (destination.empty()) {
        return true;
    }

    const auto start = timer_us();
    const auto status = esp_partition_read(
        storage->partition_, offset, destination.data(), destination.size());
    const auto elapsed = elapsed_us(start);
    record_latency(
        storage->metrics_.read_us_total,
        storage->metrics_.read_us_max,
        elapsed);
    if (status != ESP_OK) {
        ++storage->metrics_.read_failures;
        return false;
    }
    storage->metrics_.read_bytes += static_cast<std::uint64_t>(destination.size());
    return true;
}

bool BrcbFlashStorage::storage_write(
    void* context,
    const std::size_t offset,
    const std::span<const std::uint8_t> source) noexcept {
    auto* storage = static_cast<BrcbFlashStorage*>(context);
    if (storage == nullptr || !storage->ready() ||
        !range_valid(offset, source.size(), storage->geometry_.journal_bytes)) {
        return false;
    }
    ++storage->metrics_.write_calls;
    if (source.empty()) {
        return true;
    }

    const auto start = timer_us();
    const auto status = esp_partition_write(
        storage->partition_, offset, source.data(), source.size());
    const auto elapsed = elapsed_us(start);
    record_latency(
        storage->metrics_.write_us_total,
        storage->metrics_.write_us_max,
        elapsed);
    if (status != ESP_OK) {
        ++storage->metrics_.write_failures;
        return false;
    }
    storage->metrics_.write_bytes += static_cast<std::uint64_t>(source.size());
    return true;
}

bool BrcbFlashStorage::storage_erase(
    void* context,
    const std::size_t offset,
    const std::size_t bytes) noexcept {
    auto* storage = static_cast<BrcbFlashStorage*>(context);
    if (storage == nullptr || !storage->ready() || bytes == 0U ||
        !range_valid(offset, bytes, storage->geometry_.journal_bytes) ||
        (offset % storage->geometry_.erase_bytes) != 0U ||
        (bytes % storage->geometry_.erase_bytes) != 0U) {
        return false;
    }
    ++storage->metrics_.erase_calls;

    const auto start = timer_us();
    const auto status = esp_partition_erase_range(storage->partition_, offset, bytes);
    const auto elapsed = elapsed_us(start);
    record_latency(
        storage->metrics_.erase_us_total,
        storage->metrics_.erase_us_max,
        elapsed);
    if (status != ESP_OK) {
        ++storage->metrics_.erase_failures;
        return false;
    }
    storage->metrics_.erased_bytes += static_cast<std::uint64_t>(bytes);
    return true;
}

bool BrcbFlashStorage::storage_sync(void* context) noexcept {
    auto* storage = static_cast<BrcbFlashStorage*>(context);
    if (storage == nullptr || !storage->ready()) {
        return false;
    }
    // esp_partition read/write/erase calls are blocking operations. There is no
    // additional asynchronous flash queue owned by this adapter, so completion
    // of the preceding API call is the durable ordering boundary available here.
    ++storage->metrics_.sync_calls;
    return true;
}

BrcbFlashProbeResult BrcbFlashStorage::run_latency_probe(
    const std::span<const std::uint8_t> payload) noexcept {
    BrcbFlashProbeResult result;
    result.payload_bytes = payload.size();
    if (!ready()) {
        result.status = BrcbFlashProbeStatus::not_ready;
        return result;
    }
    if (payload.empty() || payload.size() > geometry_.maximum_state_bytes) {
        result.status = BrcbFlashProbeStatus::payload_too_large;
        return result;
    }

    std::array<std::uint8_t, mms::MmsStaticBrcbCheckpointStore::record_header_bytes>
        header{};
    std::array<std::uint8_t, mms::MmsStaticBrcbCheckpointStore::record_footer_bytes>
        footer{};
    for (std::size_t index = 0U; index < header.size(); ++index) {
        header[index] = static_cast<std::uint8_t>(0x40U + (index & 0x3FU));
    }
    for (std::size_t index = 0U; index < footer.size(); ++index) {
        footer[index] = static_cast<std::uint8_t>(0xA0U + (index & 0x1FU));
    }

    const auto cleanup = [&]() noexcept {
        const auto start = timer_us();
        const auto erased = esp_partition_erase_range(
            partition_, geometry_.probe_offset, geometry_.probe_bytes);
        result.cleanup_erase_us = elapsed_us(start);
        result.cleanup_succeeded = erased == ESP_OK;
        if (result.cleanup_succeeded) {
            ++result.probe_erase_units;
        }
        return result.cleanup_succeeded;
    };

    auto start = timer_us();
    auto status = esp_partition_erase_range(
        partition_, geometry_.probe_offset, geometry_.probe_bytes);
    result.prepare_erase_us = elapsed_us(start);
    if (status != ESP_OK) {
        result.status = BrcbFlashProbeStatus::prepare_erase_failed;
        return result;
    }
    ++result.probe_erase_units;

    start = timer_us();
    status = esp_partition_write(
        partition_, geometry_.probe_offset, header.data(), header.size());
    result.header_write_us = elapsed_us(start);
    if (status != ESP_OK) {
        result.status = BrcbFlashProbeStatus::header_write_failed;
        (void)cleanup();
        return result;
    }

    start = timer_us();
    status = esp_partition_write(
        partition_,
        geometry_.probe_offset + header.size(),
        payload.data(),
        payload.size());
    result.payload_write_us = elapsed_us(start);
    if (status != ESP_OK) {
        result.status = BrcbFlashProbeStatus::payload_write_failed;
        (void)cleanup();
        return result;
    }

    const auto footer_offset =
        geometry_.probe_offset + geometry_.probe_bytes - footer.size();
    start = timer_us();
    status = esp_partition_write(
        partition_, footer_offset, footer.data(), footer.size());
    result.footer_write_us = elapsed_us(start);
    if (status != ESP_OK) {
        result.status = BrcbFlashProbeStatus::footer_write_failed;
        (void)cleanup();
        return result;
    }

    std::array<std::uint8_t, mms::MmsStaticBrcbCheckpointStore::record_header_bytes>
        header_read{};
    std::array<std::uint8_t, mms::MmsStaticBrcbCheckpointStore::record_footer_bytes>
        footer_read{};
    std::array<std::uint8_t, 256U> chunk{};

    const auto read_start = timer_us();
    status = esp_partition_read(
        partition_, geometry_.probe_offset, header_read.data(), header_read.size());
    if (status != ESP_OK) {
        result.verify_read_us = elapsed_us(read_start);
        result.status = BrcbFlashProbeStatus::header_read_failed;
        (void)cleanup();
        return result;
    }

    bool payload_matches = true;
    std::size_t consumed = 0U;
    while (consumed < payload.size()) {
        const auto bytes = std::min(chunk.size(), payload.size() - consumed);
        status = esp_partition_read(
            partition_,
            geometry_.probe_offset + header.size() + consumed,
            chunk.data(),
            bytes);
        if (status != ESP_OK) {
            result.verify_read_us = elapsed_us(read_start);
            result.status = BrcbFlashProbeStatus::payload_read_failed;
            (void)cleanup();
            return result;
        }
        if (!std::equal(
                chunk.begin(),
                chunk.begin() + static_cast<std::ptrdiff_t>(bytes),
                payload.subspan(consumed, bytes).begin())) {
            payload_matches = false;
        }
        consumed += bytes;
    }

    status = esp_partition_read(
        partition_, footer_offset, footer_read.data(), footer_read.size());
    result.verify_read_us = elapsed_us(read_start);
    if (status != ESP_OK) {
        result.status = BrcbFlashProbeStatus::footer_read_failed;
        (void)cleanup();
        return result;
    }

    const auto verified =
        std::equal(header.begin(), header.end(), header_read.begin()) &&
        payload_matches &&
        std::equal(footer.begin(), footer.end(), footer_read.begin());
    if (!verified) {
        result.status = BrcbFlashProbeStatus::verify_failed;
        (void)cleanup();
        return result;
    }

    if (!cleanup()) {
        result.status = BrcbFlashProbeStatus::cleanup_erase_failed;
        return result;
    }

    result.status = BrcbFlashProbeStatus::ok;
    return result;
}

const char* brcb_flash_status_name(const BrcbFlashStatus status) noexcept {
    switch (status) {
    case BrcbFlashStatus::ok:
        return "ok";
    case BrcbFlashStatus::partition_not_found:
        return "partition-not-found";
    case BrcbFlashStatus::read_only_partition:
        return "read-only-partition";
    case BrcbFlashStatus::encrypted_partition_not_supported:
        return "encrypted-partition-not-supported";
    case BrcbFlashStatus::invalid_geometry:
        return "invalid-geometry";
    case BrcbFlashStatus::partition_too_small:
        return "partition-too-small";
    }
    return "unknown";
}

const char* brcb_flash_probe_status_name(const BrcbFlashProbeStatus status) noexcept {
    switch (status) {
    case BrcbFlashProbeStatus::ok:
        return "ok";
    case BrcbFlashProbeStatus::not_ready:
        return "not-ready";
    case BrcbFlashProbeStatus::payload_too_large:
        return "payload-too-large";
    case BrcbFlashProbeStatus::prepare_erase_failed:
        return "prepare-erase-failed";
    case BrcbFlashProbeStatus::header_write_failed:
        return "header-write-failed";
    case BrcbFlashProbeStatus::payload_write_failed:
        return "payload-write-failed";
    case BrcbFlashProbeStatus::footer_write_failed:
        return "footer-write-failed";
    case BrcbFlashProbeStatus::header_read_failed:
        return "header-read-failed";
    case BrcbFlashProbeStatus::payload_read_failed:
        return "payload-read-failed";
    case BrcbFlashProbeStatus::footer_read_failed:
        return "footer-read-failed";
    case BrcbFlashProbeStatus::verify_failed:
        return "verify-failed";
    case BrcbFlashProbeStatus::cleanup_erase_failed:
        return "cleanup-erase-failed";
    }
    return "unknown";
}

} // namespace ar::iec61850::ports::esp_idf
