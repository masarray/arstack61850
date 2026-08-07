// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/embedded/profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ar::iec61850::embedded::boards {

// Hardware profile for Waveshare ESP32-S3-POE-ETH-8DI-8DO (SKU 32108).
// Pin assignments follow the vendor board documentation. This type contains
// facts only; ESP-IDF/W5500 driver ownership stays in the platform adapter.
struct WaveshareEsp32S3PoeEth8Di8Do final {
    using capacity_profile = Esp32SmallProfile;

    static constexpr std::size_t flash_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t psram_bytes = 8U * 1024U * 1024U;

    static constexpr int w5500_interrupt_gpio = 12;
    static constexpr int w5500_mosi_gpio = 13;
    static constexpr int w5500_miso_gpio = 14;
    static constexpr int w5500_sclk_gpio = 15;
    static constexpr int w5500_chip_select_gpio = 16;
    static constexpr int w5500_reset_gpio = 39;

    // At chip level, W5500 MACRAW mode is valid on Socket 0. In the preferred
    // ESP-IDF integration the official W5500 Ethernet driver owns MACRAW and
    // feeds the software TCP/IP stack, so arstack must not independently claim
    // hardware sockets behind that driver. Raw SV/GOOSE and lwIP TCP/MMS share
    // the same Ethernet interface through the platform adapter.
    static constexpr std::uint8_t w5500_macraw_socket = 0U;

    static constexpr std::array<int, 8> digital_input_gpios{
        4, 5, 6, 7, 8, 9, 10, 11};

    // Digital outputs are exposed as EXIO1..EXIO8 through the TCA9554PWR.
    // Do not assume bit-to-terminal mapping here; the ESP-IDF board adapter
    // will verify that mapping against the vendor demo/schematic.
    static constexpr std::uint8_t output_expander_i2c_address = 0x20U;

    static constexpr int can_tx_gpio = 2;
    static constexpr int can_rx_gpio = 3;

    static constexpr int rs485_tx_gpio = 17;
    static constexpr int rs485_rx_gpio = 18;
    static constexpr int rs485_rts_gpio = 21;

    static constexpr int rtc_interrupt_gpio = 40;
    static constexpr int rtc_scl_gpio = 41;
    static constexpr int rtc_sda_gpio = 42;

    static constexpr int rgb_gpio = 38;
    static constexpr int buzzer_gpio = 46;
    static constexpr int boot_gpio = 0;

    static constexpr bool has_poe_802_3af = true;
    static constexpr bool has_w5500_macraw = true;
    static constexpr bool has_isolated_digital_inputs = true;
    static constexpr bool has_isolated_digital_outputs = true;
};

static_assert(EmbeddedCapacityProfile<
    WaveshareEsp32S3PoeEth8Di8Do::capacity_profile>);
static_assert(WaveshareEsp32S3PoeEth8Di8Do::digital_input_gpios.size() == 8U);

} // namespace ar::iec61850::embedded::boards
