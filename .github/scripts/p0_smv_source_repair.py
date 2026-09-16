from pathlib import Path


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, before, after):
    if before != after:
        Path(path).write_text(after, encoding="utf-8")


def one(text, old, new, label):
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


path = "embedded/esp32p4_smv_injector/main/app_main.cpp"
before = read(path)
after = before
after = one(after,
    "using ar::esp32p4::smv::live_control_bind_publisher_task;\n",
    "using ar::esp32p4::smv::live_control_bind_publisher_task;\nusing ar::esp32p4::smv::live_control_force_stop;\n",
    "SMV force-stop import")
after = one(after,
    "constexpr std::uint32_t kStatsMergeEvery = 400U;\n",
    "constexpr std::uint32_t kStatsMergeEvery = 400U;\nconstexpr std::uint32_t kTransportFailureStopThreshold = 400U; // 100 ms at 4 kHz\n",
    "SMV transport threshold")
after = one(after,
    "    bool schedule_anchored = false;\n    std::int64_t expected_wake_us = 0;\n",
    "    bool schedule_anchored = false;\n    std::int64_t expected_wake_us = 0;\n    std::uint32_t consecutive_canonical_tx_failures = 0U;\n    esp_err_t last_canonical_tx_error = ESP_OK;\n",
    "SMV transport state")
after = one(after,
    "            schedule_anchored = false;\n            expected_schedule.reset(active_profile.publisher_rate_hz);\n",
    "            schedule_anchored = false;\n            consecutive_canonical_tx_failures = 0U;\n            last_canonical_tx_error = ESP_OK;\n            expected_schedule.reset(active_profile.publisher_rate_hz);\n",
    "SMV START health reset")
after = one(after,
"""        if ((xEventGroupGetBits(g_link_events) & kLinkUpBit) == 0U) {
            ++local.canonical_fail;
#if CONFIG_AR_SMV_BROADCAST_MIRROR
            ++local.mirror_fail;
#endif
        } else {
            patch_packet(canonical, canonical_sample_count, row, signal_state);
            const esp_err_t canonical_result =
                esp_eth_transmit(eth_handle, canonical.bytes.data(), canonical.bytes.size());
            if (canonical_result == ESP_OK) ++local.canonical_ok;
            else ++local.canonical_fail;
""",
"""        if ((xEventGroupGetBits(g_link_events) & kLinkUpBit) == 0U) {
            ++local.canonical_fail;
            last_canonical_tx_error = ESP_ERR_INVALID_STATE;
            ++consecutive_canonical_tx_failures;
#if CONFIG_AR_SMV_BROADCAST_MIRROR
            ++local.mirror_fail;
#endif
        } else {
            patch_packet(canonical, canonical_sample_count, row, signal_state);
            const esp_err_t canonical_result =
                esp_eth_transmit(eth_handle, canonical.bytes.data(), canonical.bytes.size());
            if (canonical_result == ESP_OK) {
                ++local.canonical_ok;
                consecutive_canonical_tx_failures = 0U;
                last_canonical_tx_error = ESP_OK;
            } else {
                ++local.canonical_fail;
                last_canonical_tx_error = canonical_result;
                ++consecutive_canonical_tx_failures;
            }
""",
    "SMV canonical TX health")
after = one(after,
"""        g_sample_tick_total.fetch_add(1U, std::memory_order_relaxed);
        canonical_sample_count = static_cast<std::uint16_t>(
""",
"""        g_sample_tick_total.fetch_add(1U, std::memory_order_relaxed);
        if (consecutive_canonical_tx_failures >= kTransportFailureStopThreshold) {
            ESP_LOGE(kTag,
                     "SV transport fault: %lu consecutive canonical TX failures; last=%s; forcing STOP",
                     static_cast<unsigned long>(consecutive_canonical_tx_failures),
                     esp_err_to_name(last_canonical_tx_error));
            merge_stats(local);
            live_control_force_stop();
            stop_sample_clock(sample_clock);
            schedule_anchored = false;
            consecutive_canonical_tx_failures = 0U;
            continue;
        }
        canonical_sample_count = static_cast<std::uint16_t>(
""",
    "SMV fail-closed runtime")
write(path, before, after)

path = "embedded/esp32p4_smv_injector/main/live_control.cpp"
before = read(path)
after = one(before,
"""    clear_control_session();
    wake_publisher();
}

SvLiveSignalState live_signal_snapshot() noexcept {
""",
"""    clear_control_session();
    wake_publisher();
    ESP_LOGW(kTag, "STOP accepted: SV transmission suppressed by fail-closed runtime");
}

SvLiveSignalState live_signal_snapshot() noexcept {
""",
    "forced STOP observability")
write(path, before, after)

path = "embedded/esp32p4_smv_injector/main/ptp_lab_task.cpp"
before = read(path)
after = before
helper = """void disable_hardware_ptp(const esp_eth_handle_t eth_handle) noexcept {
    if (eth_handle == nullptr) return;
    bool enable = false;
    const auto result = esp_eth_ioctl(
        eth_handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_PTP_ENABLE),
        &enable);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Unable to restore normal EMAC mode after PTP SOURCE: %s",
                 esp_err_to_name(result));
    }
}

"""
if "void disable_hardware_ptp(const esp_eth_handle_t eth_handle)" not in after:
    anchor = "void seed_hardware_clock(const esp_eth_handle_t eth_handle) noexcept {"
    if anchor not in after:
        raise SystemExit("PTP SOURCE hardware-clock anchor missing")
    after = after.replace(anchor, helper + anchor, 1)
after = one(after,
"""    context.task_handle = nullptr;
    g_ptp_ready.store(false, std::memory_order_release);
""",
"""    disable_hardware_ptp(context.eth_handle);
    context.task_handle = nullptr;
    g_ptp_ready.store(false, std::memory_order_release);
""",
    "PTP SOURCE EMAC rollback")
after = one(after,
"""    if (g_ptp_context.task_handle != nullptr) {
        xTaskNotifyGive(g_ptp_context.task_handle);
    }
}

[[nodiscard]] bool ptp_lab_is_running() noexcept {
""",
"""    if (g_ptp_context.task_handle != nullptr) {
        xTaskNotifyGive(g_ptp_context.task_handle);
    }
    constexpr unsigned kCleanupPolls = 100U;
    for (unsigned attempt = 0U; attempt < kCleanupPolls; ++attempt) {
        if (!g_ptp_started.load(std::memory_order_acquire)) return;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGE(kTag, "PTP SOURCE cleanup timeout; EMAC rollback incomplete after 500 ms");
}

[[nodiscard]] bool ptp_lab_is_running() noexcept {
""",
    "PTP SOURCE bounded STOP")
write(path, before, after)

path = "embedded/esp32p4_smv_injector/main/ptp_receiver_task.cpp"
before = read(path)
after = before
helper = """void disable_hardware_ptp(const esp_eth_handle_t handle) noexcept {
    if (handle == nullptr) return;
    bool enable = false;
    const auto result = esp_eth_ioctl(
        handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_PTP_ENABLE),
        &enable);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Unable to restore normal EMAC mode after PTP receiver/monitor: %s",
                 esp_err_to_name(result));
    }
}

"""
if "void disable_hardware_ptp(const esp_eth_handle_t handle)" not in after:
    anchor = "void seed_hardware_clock(const esp_eth_handle_t handle) noexcept {"
    if anchor not in after:
        raise SystemExit("PTP receiver hardware-clock anchor missing")
    after = after.replace(anchor, helper + anchor, 1)
after = one(after,
"""    smp_synch_lab_set_measured(std::nullopt);
    context.receiver.reset();
""",
"""    smp_synch_lab_set_measured(std::nullopt);
    disable_hardware_ptp(context.eth_handle);
    context.receiver.reset();
""",
    "PTP receiver EMAC rollback")
after = one(after,
"""    if (g_receiver_context.task_handle != nullptr) {
        xTaskNotifyGive(g_receiver_context.task_handle);
    }
}

bool ptp_receiver_is_running() noexcept {
""",
"""    if (g_receiver_context.task_handle != nullptr) {
        xTaskNotifyGive(g_receiver_context.task_handle);
    }
    constexpr unsigned kCleanupPolls = 100U;
    for (unsigned attempt = 0U; attempt < kCleanupPolls; ++attempt) {
        if (!g_receiver_running.load(std::memory_order_acquire)) return;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGE(kTag, "PTP receiver cleanup timeout; EMAC rollback incomplete after 500 ms");
}

bool ptp_receiver_is_running() noexcept {
""",
    "PTP receiver bounded STOP")
write(path, before, after)

for file, tokens in {
    "embedded/esp32p4_smv_injector/main/app_main.cpp": ["kTransportFailureStopThreshold = 400U", "live_control_force_stop();", "SV transport fault:"],
    "embedded/esp32p4_smv_injector/main/ptp_lab_task.cpp": ["disable_hardware_ptp(context.eth_handle);", "PTP SOURCE cleanup timeout"],
    "embedded/esp32p4_smv_injector/main/ptp_receiver_task.cpp": ["disable_hardware_ptp(context.eth_handle);", "PTP receiver cleanup timeout"],
}.items():
    data = read(file)
    missing = [token for token in tokens if token not in data]
    if missing:
        raise SystemExit(f"{file}: source repair verification failed: {missing}")

print("P0 SMV/PTP source repair applied and verified")