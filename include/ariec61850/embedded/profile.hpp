// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace ar::iec61850::embedded {

// Conservative starting capacities for a small IEC 61850 I/O IED or SV
// publisher. Applications may provide their own profile type; these values are
// not protocol limits and must not be treated as negotiated MMS capabilities.
struct Esp32SmallProfile final {
    static constexpr std::size_t ethernet_frame_bytes = 1'536U;
    static constexpr std::size_t mms_rx_bytes = 8'192U;
    static constexpr std::size_t mms_tx_bytes = 8'192U;
    static constexpr std::size_t maximum_logical_devices = 4U;
    static constexpr std::size_t maximum_logical_nodes = 32U;
    static constexpr std::size_t maximum_data_sets = 16U;
    static constexpr std::size_t maximum_data_set_members = 64U;
    static constexpr std::size_t maximum_report_controls = 16U;
    static constexpr std::size_t maximum_sv_asdus_per_frame = 8U;
};

template <typename Profile>
concept EmbeddedCapacityProfile =
    requires {
        Profile::ethernet_frame_bytes;
        Profile::mms_rx_bytes;
        Profile::mms_tx_bytes;
        Profile::maximum_logical_devices;
        Profile::maximum_logical_nodes;
        Profile::maximum_data_sets;
        Profile::maximum_data_set_members;
        Profile::maximum_report_controls;
        Profile::maximum_sv_asdus_per_frame;
    };

static_assert(EmbeddedCapacityProfile<Esp32SmallProfile>);
static_assert(Esp32SmallProfile::ethernet_frame_bytes >= 1'522U);

} // namespace ar::iec61850::embedded
