// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/mms/utc_time.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::sampled_values {

struct SampledValueAsdu final {
    std::string sv_id;
    std::string data_set_reference;
    std::uint16_t sample_count{};
    std::uint32_t configuration_revision{1U};
    std::optional<mms::Iec61850UtcTime> reference_time;
    // Synchronization claims are evidence-driven. New ASDUs therefore start
    // unsynchronized and must be promoted explicitly by a validated policy.
    std::uint8_t sample_synchronization{0U};
    std::optional<std::uint16_t> sample_rate;
    std::optional<std::uint16_t> sample_mode;
    // IEC 61850-9-2 Ed.2.1: SynchSrcID/gmIdentity, eight bytes in the
    // same network order as the PTP GrandmasterIdentity in Announce.
    std::optional<std::array<std::uint8_t, 8>> gm_identity;
    std::vector<std::uint8_t> sample_payload;

    friend bool operator==(const SampledValueAsdu&, const SampledValueAsdu&) = default;
};

struct SampledValuesPdu final {
    std::vector<SampledValueAsdu> asdus;

    friend bool operator==(const SampledValuesPdu&, const SampledValuesPdu&) = default;
};

} // namespace ar::iec61850::sampled_values
