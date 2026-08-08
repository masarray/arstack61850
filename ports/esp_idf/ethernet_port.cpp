// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ports/esp_idf/ethernet_port.hpp"

#include "esp_err.h"
#include "esp_timer.h"

#include <cstdint>

namespace ar::iec61850::ports::esp_idf {

embedded::IoResult raw_ethernet_transmit(
    void* context,
    const std::span<const std::uint8_t> frame) noexcept {
    auto* adapter = static_cast<RawEthernetContext*>(context);
    if (adapter == nullptr || adapter->handle == nullptr || frame.empty()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }

    // ESP-IDF's API currently accepts a mutable void* even though transmit does
    // not require arstack to modify the frame. Keep the const_cast contained in
    // this platform boundary instead of leaking it into the protocol core.
    const auto result = esp_eth_transmit(
        adapter->handle,
        const_cast<std::uint8_t*>(frame.data()),
        frame.size());

    if (result == ESP_OK) {
        return {embedded::IoStatus::ok, frame.size()};
    }
    if (result == ESP_ERR_TIMEOUT) {
        return {embedded::IoStatus::timeout, 0U};
    }
    if (result == ESP_ERR_INVALID_ARG) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }
    if (result == ESP_ERR_INVALID_STATE) {
        return {embedded::IoStatus::closed, 0U};
    }
    return {embedded::IoStatus::io_error, 0U};
}

embedded::RawEthernetPort make_raw_ethernet_port(
    RawEthernetContext& context) noexcept {
    return {&context, &raw_ethernet_transmit};
}

std::uint64_t monotonic_microseconds(void*) noexcept {
    const auto value = esp_timer_get_time();
    return value <= 0 ? 0U : static_cast<std::uint64_t>(value);
}

embedded::Clock make_monotonic_clock() noexcept {
    return {nullptr, &monotonic_microseconds, nullptr};
}

} // namespace ar::iec61850::ports::esp_idf
