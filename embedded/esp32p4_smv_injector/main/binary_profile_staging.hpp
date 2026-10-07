// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/compiled_device_profile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::esp32p4::smv {

// Transfer-only storage, never an alternative active SV profile authority.
// Fixed bounds fit the legacy 192-byte ASCII console using <=48-byte chunks.
class BinaryProfileStaging final {
public:
    static constexpr std::size_t max_record_bytes =
        ar::iec61850::sampled_values::SvDeviceProfileBinaryCodec::header_bytes +
        ar::iec61850::sampled_values::SvDeviceProfileBinaryCodec::fixed_payload_bytes +
        ar::iec61850::sampled_values::SvDeviceProfileBinaryCodec::leaf_descriptor_bytes *
            ar::iec61850::sampled_values::compiled_sv_device_profile_max_leaves +
        ar::iec61850::sampled_values::compiled_sv_device_profile_max_sv_id_bytes +
        ar::iec61850::sampled_values::compiled_sv_device_profile_max_dataset_bytes;
    static constexpr std::size_t max_chunk_bytes = 48U;
    static constexpr std::size_t min_record_bytes =
        ar::iec61850::sampled_values::SvDeviceProfileBinaryCodec::header_bytes +
        ar::iec61850::sampled_values::SvDeviceProfileBinaryCodec::fixed_payload_bytes +
        ar::iec61850::sampled_values::SvDeviceProfileBinaryCodec::leaf_descriptor_bytes;

    // BINCHUNK worst-case ASCII length, including decimal token/offset.
    static_assert(17U + 10U + 1U + 10U + 1U + 2U * max_chunk_bytes < 192U);

    [[nodiscard]] bool begin(const std::uint32_t transaction,
                             const std::size_t total_bytes) noexcept {
        if (active_ || transaction == 0U || transaction <= last_transaction_ ||
            total_bytes < min_record_bytes || total_bytes > max_record_bytes) {
            return false;
        }
        data_.fill(std::uint8_t{0U});
        expected_ = total_bytes;
        received_ = 0U;
        transaction_ = transaction;
        last_transaction_ = transaction;
        active_ = true;
        return true;
    }

    [[nodiscard]] bool owns(const std::uint32_t transaction) const noexcept {
        return active_ && transaction != 0U && transaction == transaction_;
    }

    [[nodiscard]] bool append(const std::uint32_t transaction,
                              const std::size_t offset,
                              const std::span<const std::uint8_t> bytes) noexcept {
        if (!owns(transaction)) return false;
        if (offset != received_ || bytes.empty() ||
            bytes.size() > max_chunk_bytes ||
            bytes.size() > expected_ - received_) {
            abort();
            return false;
        }
        std::copy(bytes.begin(), bytes.end(), data_.begin() + received_);
        received_ += bytes.size();
        return true;
    }

    [[nodiscard]] bool complete(const std::uint32_t transaction) const noexcept {
        return owns(transaction) && received_ == expected_;
    }

    [[nodiscard]] std::span<const std::uint8_t> complete_record(
        const std::uint32_t transaction) const noexcept {
        return complete(transaction)
            ? std::span<const std::uint8_t>{data_}.first(expected_)
            : std::span<const std::uint8_t>{};
    }

    [[nodiscard]] std::size_t received() const noexcept { return received_; }
    [[nodiscard]] std::size_t expected() const noexcept { return expected_; }
    [[nodiscard]] std::uint32_t transaction() const noexcept { return transaction_; }
    [[nodiscard]] std::uint32_t last_transaction() const noexcept { return last_transaction_; }
    [[nodiscard]] bool active() const noexcept { return active_; }

    void abort() noexcept {
        data_.fill(std::uint8_t{0U});
        expected_ = 0U;
        received_ = 0U;
        transaction_ = 0U;
        active_ = false;
        // Keep last_transaction_ to reject replay inside this console session.
    }

private:
    std::array<std::uint8_t, max_record_bytes> data_{};
    std::size_t expected_{};
    std::size_t received_{};
    std::uint32_t transaction_{};
    std::uint32_t last_transaction_{};
    bool active_{};
};

} // namespace ar::esp32p4::smv
