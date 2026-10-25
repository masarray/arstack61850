// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/m4_three_sv_binary.hpp"
#include "ariec61850/sampled_values/profile_identity.hpp"
#include "binary_profile_staging.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::esp32p4::smv {

// Control-plane ONLY until M4-P0C2 connects this roster to the shared
// PTP-grid publisher. Never pretend that COMMIT starts three Ethernet streams.
// The board holds two bounded banks: staged bytes and committed profiles;
// no XML, dynamic memory, or reallocations on ESP32-P4.
class MultiSvConfigStore final {
public:
    using DeviceProfile = ar::iec61850::sampled_values::CompiledSvDeviceProfile;
    using Profiles = std::array<
        DeviceProfile, ar::iec61850::sampled_values::m4_three_sv_capacity>;

    static constexpr std::size_t max_chunk_bytes = BinaryProfileStaging::max_chunk_bytes;
    static constexpr std::size_t min_record_bytes =
        ar::iec61850::sampled_values::M4ThreeSvBinaryCodec::header_bytes +
        ar::iec61850::sampled_values::m4_three_sv_capacity *
            BinaryProfileStaging::min_record_bytes;
    static constexpr std::size_t max_record_bytes =
        ar::iec61850::sampled_values::M4ThreeSvBinaryCodec::header_bytes +
        ar::iec61850::sampled_values::m4_three_sv_capacity *
            BinaryProfileStaging::max_record_bytes;
    static_assert(max_record_bytes < 4096U);
    static_assert(20U + 10U + 1U + 10U + 1U + 2U * max_chunk_bytes < 192U);

    [[nodiscard]] bool begin(const std::uint32_t transaction,
                             const std::size_t total) noexcept {
        if (uploading_ || transaction == 0U || transaction <= last_transaction_ ||
            total < min_record_bytes || total > max_record_bytes) return false;
        staging_bytes_.fill(0U);
        expected_ = total;
        received_ = 0U;
        transaction_ = transaction;
        last_transaction_ = transaction; // reject replay even after aborted transfer
        uploading_ = true;
        validated_ = false;
        return true;
    }

    [[nodiscard]] bool owns(const std::uint32_t transaction) const noexcept {
        return uploading_ && transaction != 0U && transaction == transaction_;
    }

    [[nodiscard]] bool append(const std::uint32_t transaction,
                              const std::size_t offset,
                              const std::span<const std::uint8_t> bytes) noexcept {
        if (!owns(transaction) || offset != received_ || bytes.empty() ||
            bytes.size() > max_chunk_bytes ||
            bytes.size() > expected_ - received_ || validated_) {
            abort();
            return false;
        }
        std::copy(bytes.begin(), bytes.end(), staging_bytes_.begin() + received_);
        received_ += bytes.size();
        return true;
    }

    [[nodiscard]] bool complete(const std::uint32_t transaction) const noexcept {
        return owns(transaction) && received_ == expected_;
    }

    [[nodiscard]] bool validate(const std::uint32_t transaction) noexcept {
        validated_ = false;
        if (!complete(transaction)) return false;
        const auto decoded = ar::iec61850::sampled_values::M4ThreeSvBinaryCodec::decode(
            std::span<const std::uint8_t>{staging_bytes_}.first(expected_),
            candidate_);
        validated_ = decoded.success() && decoded.bytes == expected_;
        return validated_;
    }

    [[nodiscard]] bool commit(const std::uint32_t transaction) noexcept {
        if (!complete(transaction) || !validated_) return false;
        const auto bytes = std::span<const std::uint8_t>{staging_bytes_}.first(expected_);
        // Whole-roster candidate was decoded AND cross-validated by the V2 codec.
        // Publishing task does not yet read this bank: no partial-on-wire mutation.
        committed_profiles_ = candidate_;
        committed_hash_ =
            ar::iec61850::sampled_values::sv_binary_profile_identity_hash(bytes);
        committed_bytes_ = static_cast<std::uint32_t>(bytes.size());
        ++generation_;
        configured_ = true;
        abort();
        return true;
    }

    void abort() noexcept {
        staging_bytes_.fill(0U);
        expected_ = 0U;
        received_ = 0U;
        transaction_ = 0U;
        uploading_ = false;
        validated_ = false;
        // Committed bank and last transaction persist across aborted uploads.
    }

    [[nodiscard]] bool uploading() const noexcept { return uploading_; }
    [[nodiscard]] bool validated() const noexcept { return validated_; }
    [[nodiscard]] bool configured() const noexcept { return configured_; }
    [[nodiscard]] std::uint32_t transaction() const noexcept { return transaction_; }
    [[nodiscard]] std::uint32_t last_transaction() const noexcept { return last_transaction_; }
    [[nodiscard]] std::size_t received() const noexcept { return received_; }
    [[nodiscard]] std::size_t expected() const noexcept { return expected_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::uint64_t committed_hash() const noexcept { return committed_hash_; }
    [[nodiscard]] std::uint32_t committed_bytes() const noexcept { return committed_bytes_; }
    [[nodiscard]] const Profiles& committed_profiles() const noexcept {
        return committed_profiles_;
    }

private:
    std::array<std::uint8_t, max_record_bytes> staging_bytes_{};
    Profiles candidate_{};
    Profiles committed_profiles_{};
    std::size_t expected_{};
    std::size_t received_{};
    std::uint32_t transaction_{};
    std::uint32_t last_transaction_{};
    std::uint32_t committed_bytes_{};
    std::uint64_t generation_{};
    std::uint64_t committed_hash_{};
    bool uploading_{};
    bool validated_{};
    bool configured_{};
};

} // namespace ar::esp32p4::smv
