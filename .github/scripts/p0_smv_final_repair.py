from pathlib import Path

changed = []


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, before, after):
    if before != after:
        Path(path).write_text(after, encoding="utf-8")
        changed.append(path)


def one(text, old, new, label):
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


# SMV transport: force STOP instead of reporting RUNNING indefinitely when
# canonical Ethernet transmission continuously fails. 400 frames = 100 ms at 4 kHz.
path = "embedded/esp32p4_smv_injector/main/app_main.cpp"
before = read(path)
after = before
after = one(
    after,
    "using ar::esp32p4::smv::live_control_bind_publisher_task;\n",
    "using ar::esp32p4::smv::live_control_bind_publisher_task;\n"
    "using ar::esp32p4::smv::live_control_force_stop;\n",
    "SMV force-stop import",
)
after = one(
    after,
    "constexpr std::uint32_t kStatsMergeEvery = 400U;\n",
    "constexpr std::uint32_t kStatsMergeEvery = 400U;\n"
    "constexpr std::uint32_t kTransportFailureStopThreshold = 400U; // 100 ms at 4 kHz\n",
    "SMV transport failure threshold",
)
after = one(
    after,
    "    bool schedule_anchored = false;\n    std::int64_t expected_wake_us = 0;\n",
    "    bool schedule_anchored = false;\n"
    "    std::int64_t expected_wake_us = 0;\n"
    "    std::uint32_t consecutive_canonical_tx_failures = 0U;\n"
    "    esp_err_t last_canonical_tx_error = ESP_OK;\n",
    "SMV transport state",
)
after = one(
    after,
    "            schedule_anchored = false;\n            expected_schedule.reset(active_profile.publisher_rate_hz);\n",
    "            schedule_anchored = false;\n"
    "            consecutive_canonical_tx_failures = 0U;\n"
    "            last_canonical_tx_error = ESP_OK;\n"
    "            expected_schedule.reset(active_profile.publisher_rate_hz);\n",
    "SMV START health reset",
)
after = one(
    after,
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
    "SMV canonical TX health tracking",
)
after = one(
    after,
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
    "SMV fail-closed runtime",
)
write(path, before, after)

path = "embedded/esp32p4_smv_injector/main/live_control.cpp"
before = read(path)
after = one(
    before,
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
    "forced STOP observability",
)
write(path, before, after)

# PTP SOURCE: IEEE1588 hardware mode is shared EMAC state. Restore normal mode
# before teardown is published and make STOP bounded.
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
after = one(
    after,
    """    context.task_handle = nullptr;
    g_ptp_ready.store(false, std::memory_order_release);
""",
    """    disable_hardware_ptp(context.eth_handle);
    context.task_handle = nullptr;
    g_ptp_ready.store(false, std::memory_order_release);
""",
    "PTP SOURCE EMAC rollback",
)
after = one(
    after,
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
    "PTP SOURCE bounded STOP",
)
write(path, before, after)

# PTP RECEIVER/MONITOR: same shared-hardware cleanup contract.
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
after = one(
    after,
    """    smp_synch_lab_set_measured(std::nullopt);
    context.receiver.reset();
""",
    """    smp_synch_lab_set_measured(std::nullopt);
    disable_hardware_ptp(context.eth_handle);
    context.receiver.reset();
""",
    "PTP receiver EMAC rollback",
)
after = one(
    after,
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
    "PTP receiver bounded STOP",
)
write(path, before, after)

# Studio CI fixture must use the production 0.1.1 contract.
path = ".github/workflows/arstack-studio-qt.yml"
before = read(path)
after = before.replace("arstack-esp32p4-smv-0.1.0.bin", "arstack-esp32p4-smv-0.1.1.bin")
after = after.replace('"version": "0.1.0",', '"version": "0.1.1",')
if '"capabilities": ["SMV-4I4V", "PROFILE", "LIVE-SETPOINTS", "SESSION-LEASE", "PTP-P2", "SMPSYNCH-AUTO"]' not in after:
    after = one(
        after,
        '            "protocol": 1,\n            "flashOffset": 0,\n',
        '            "protocol": 1,\n'
        '            "capabilities": ["SMV-4I4V", "PROFILE", "LIVE-SETPOINTS", "SESSION-LEASE", "PTP-P2", "SMPSYNCH-AUTO"],\n'
        '            "flashOffset": 0,\n',
        "Studio production manifest capabilities",
    )
write(path, before, after)

# PTP CI was asserting an obsolete source spelling instead of the contract.
path = ".github/workflows/esp32p4-ptp-lab.yml"
before = read(path)
after = before.replace(
    "              'return ar_ptp_lab_try_start(s_eth_handle);',",
    "              'const bool started = ar_ptp_lab_try_start(s_eth_handle);',",
)
if after == before:
    raise SystemExit("PTP workflow current-start assertion anchor missing")
write(path, before, after)

# Production CI must retain shared-TX serialization and fail-closed transport.
path = ".github/workflows/esp32p4-smv-injector.yml"
before = read(path)
after = before
if "grep -qx 'CONFIG_ETH_TRANSMIT_MUTEX=y' sdkconfig" not in after:
    after = one(
        after,
        "          grep -qx 'CONFIG_AR_PTP_LAB_TX=y' sdkconfig\n",
        "          grep -qx 'CONFIG_ETH_TRANSMIT_MUTEX=y' sdkconfig\n"
        "          grep -qx 'CONFIG_AR_PTP_LAB_TX=y' sdkconfig\n",
        "production TX serialization CI gate",
    )
if "assert 'kTransportFailureStopThreshold = 400U' in source" not in after:
    after = one(
        after,
        "          assert 'rate_baseline_valid = true;' in source\n",
        "          assert 'rate_baseline_valid = true;' in source\n"
        "          assert 'kTransportFailureStopThreshold = 400U' in source\n"
        "          assert 'live_control_force_stop();' in source\n"
        "          assert 'SV transport fault:' in source\n",
        "SMV fail-closed CI assertions",
    )
write(path, before, after)

required = {
    "embedded/esp32p4_smv_injector/main/app_main.cpp": [
        "kTransportFailureStopThreshold = 400U",
        "live_control_force_stop();",
        "SV transport fault:",
    ],
    "embedded/esp32p4_smv_injector/main/ptp_lab_task.cpp": [
        "disable_hardware_ptp(context.eth_handle);",
        "PTP SOURCE cleanup timeout",
    ],
    "embedded/esp32p4_smv_injector/main/ptp_receiver_task.cpp": [
        "disable_hardware_ptp(context.eth_handle);",
        "PTP receiver cleanup timeout",
    ],
    ".github/workflows/arstack-studio-qt.yml": [
        "arstack-esp32p4-smv-0.1.1.bin",
        '"PTP-P2", "SMPSYNCH-AUTO"',
    ],
    ".github/workflows/esp32p4-smv-injector.yml": ["CONFIG_ETH_TRANSMIT_MUTEX=y"],
}
for file, tokens in required.items():
    data = read(file)
    missing = [token for token in tokens if token not in data]
    if missing:
        raise SystemExit(f"{file}: repair verification failed: {missing}")

print("Patched files:")
for item in changed:
    print(" -", item)
