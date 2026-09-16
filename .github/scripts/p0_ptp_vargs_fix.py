from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f'{label}: expected source block not found')
    return text.replace(old, new, 1)

lab_path = Path('embedded/esp32p4_smv_injector/main/ptp_lab_task.cpp')
lab = lab_path.read_text(encoding='utf-8')
old = '''[[nodiscard]] bool send_frame(
    const esp_eth_handle_t eth_handle,
    std::vector<std::uint8_t>& frame) noexcept {
    if (frame.empty()) return false;
    return esp_eth_transmit(eth_handle, frame.data(), frame.size()) == ESP_OK;
}

[[nodiscard]] bool transmit_with_hw_timestamp(
    const esp_eth_handle_t eth_handle,
    std::vector<std::uint8_t>& frame,
    PtpTimestamp& timestamp) noexcept {
    if (frame.empty()) return false;
    eth_mac_time_t tx_timestamp{};
    // esp_eth_transmit_ctrl_vargs argc counts buffer/length pairs. One PTP
    // Ethernet frame is exactly one pair.
    const auto result = esp_eth_transmit_ctrl_vargs(
        eth_handle,
        &tx_timestamp,
        1U,
        frame.data(),
        frame.size());
    if (result != ESP_OK || !valid_hw_timestamp(tx_timestamp)) {
        return false;
    }
    timestamp = to_ptp_timestamp(tx_timestamp);
    return true;
}
'''
new = '''[[nodiscard]] bool send_frame(
    const esp_eth_handle_t eth_handle,
    std::vector<std::uint8_t>& frame) noexcept {
    if (frame.empty()) return false;
    const auto result = esp_eth_transmit(eth_handle, frame.data(), frame.size());
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "PTP general TX failed: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

[[nodiscard]] bool transmit_with_hw_timestamp(
    const esp_eth_handle_t eth_handle,
    std::vector<std::uint8_t>& frame,
    PtpTimestamp& timestamp) noexcept {
    if (frame.empty()) return false;
    eth_mac_time_t tx_timestamp{};
    // ESP-IDF 5.5 forwards argc as the number of variadic arguments. The
    // ESP32-P4 EMAC implementation computes buf_num = argc / 2, therefore one
    // Ethernet frame requires two arguments: buffer pointer + buffer length.
    const auto result = esp_eth_transmit_ctrl_vargs(
        eth_handle,
        &tx_timestamp,
        2U,
        frame.data(),
        frame.size());
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "PTP hardware-timestamp TX failed: %s", esp_err_to_name(result));
        return false;
    }
    if (!valid_hw_timestamp(tx_timestamp)) {
        ESP_LOGE(kTag,
                 "PTP TX completed without a valid hardware timestamp: sec=%lu ns=%lu",
                 static_cast<unsigned long>(tx_timestamp.seconds),
                 static_cast<unsigned long>(tx_timestamp.nanoseconds));
        return false;
    }
    timestamp = to_ptp_timestamp(tx_timestamp);
    return true;
}
'''
lab = replace_once(lab, old, new, 'PTP source varargs contract')
lab_path.write_text(lab, encoding='utf-8')

receiver_path = Path('embedded/esp32p4_smv_injector/main/ptp_receiver_task.cpp')
receiver = receiver_path.read_text(encoding='utf-8')
old = '''    eth_mac_time_t tx_timestamp{};
    // esp_eth_transmit_ctrl_vargs argc counts buffer/length pairs. One PTP
    // Ethernet frame is exactly one pair.
    const auto result = esp_eth_transmit_ctrl_vargs(
        handle,
        &tx_timestamp,
        1U,
        frame.data(),
        frame.size());
    if (result != ESP_OK || !valid_hw_timestamp(tx_timestamp)) return false;
    timestamp = to_ptp_timestamp(tx_timestamp);
'''
new = '''    eth_mac_time_t tx_timestamp{};
    // ESP-IDF 5.5 forwards argc as the number of variadic arguments. The
    // ESP32-P4 EMAC implementation computes buf_num = argc / 2, therefore one
    // Ethernet frame requires two arguments: buffer pointer + buffer length.
    const auto result = esp_eth_transmit_ctrl_vargs(
        handle,
        &tx_timestamp,
        2U,
        frame.data(),
        frame.size());
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "PTP receiver hardware-timestamp TX failed: %s", esp_err_to_name(result));
        return false;
    }
    if (!valid_hw_timestamp(tx_timestamp)) {
        ESP_LOGE(kTag,
                 "PTP receiver TX completed without a valid hardware timestamp: sec=%lu ns=%lu",
                 static_cast<unsigned long>(tx_timestamp.seconds),
                 static_cast<unsigned long>(tx_timestamp.nanoseconds));
        return false;
    }
    timestamp = to_ptp_timestamp(tx_timestamp);
'''
receiver = replace_once(receiver, old, new, 'PTP receiver varargs contract')
receiver_path.write_text(receiver, encoding='utf-8')

print('PTP ESP-IDF 5.5 varargs contract + diagnostics patch applied')
