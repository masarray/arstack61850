// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/embedded/io.hpp"

#include "esp_eth.h"

namespace ar::iec61850::ports::esp_idf {

// Small adapter context owned by the ESP-IDF application. The protocol core
// never sees esp_eth_handle_t; only this platform adapter does.
struct RawEthernetContext final {
    esp_eth_handle_t handle{};
};

[[nodiscard]] embedded::IoResult raw_ethernet_transmit(
    void* context,
    std::span<const std::uint8_t> frame) noexcept;

[[nodiscard]] embedded::RawEthernetPort make_raw_ethernet_port(
    RawEthernetContext& context) noexcept;

[[nodiscard]] std::uint64_t monotonic_microseconds(void* context) noexcept;

[[nodiscard]] embedded::Clock make_monotonic_clock() noexcept;

} // namespace ar::iec61850::ports::esp_idf
