#!/usr/bin/env python3
"""Static ownership guard for the ESP32-P4 autonomous-publishing contract.

Behavioral signal/timing regression stays in the native suites; this test
forbids silently reintroducing USB heartbeat as a publisher STOP authority.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
fw = (root / "embedded/esp32p4_smv_injector/main/live_control.cpp").read_text()
device = (root / "apps/arstack_studio/src/DeviceController.cpp").read_text()
eth = (root / "embedded/esp32p4_smv_injector/main/app_main.cpp").read_text()
ptp = (root / "embedded/esp32p4_smv_injector/main/ptp_lab_task.cpp").read_text()
facade = (root / "embedded/esp32p4_smv_injector/main/ptp_mode_facade.cpp").read_text()
receiver = (root / "embedded/esp32p4_smv_injector/main/ptp_receiver_task.cpp").read_text()
assert "void enforce_control_lease() noexcept" in fw
body = fw.split("void enforce_control_lease() noexcept", 1)[1].split(
    "void control_lease_timer_callback", 1)[0]
for forbidden in ("g_start_request.store(false", "g_running.exchange(false",
                  "record_sv_stop(", "wake_publisher()"):
    assert forbidden not in body, f"USB lost publishing autonomy: {forbidden}"
assert "void record_control_heartbeat()" in fw
assert 'std::strcmp(command, "STOP") == 0' in fw
assert "SvStopCause::operator_request" in fw
assert "const bool requestSvStop = false;" in device
assert "live_control_set_ethernet_link(true);" in eth
assert eth.count("live_control_set_ethernet_link(false);") >= 2
assert "case ETHERNET_EVENT_DISCONNECTED:" in eth
assert "case ETHERNET_EVENT_STOP:" in eth
driver_stop = eth.split("case ETHERNET_EVENT_STOP:", 1)[1].split("default:", 1)[0]
assert "xEventGroupClearBits(g_link_events, kLinkUpBit);" in driver_stop
assert "live_control_set_ethernet_link(false);" in driver_stop
assert "ar_ptp_lab_request_link_down_stop();" in driver_stop

start = fw.split('if (std::strcmp(command, "START") == 0)', 1)[1].split(
    'if (std::strcmp(command, "STOP") == 0)', 1)[0]
assert "portENTER_CRITICAL(&g_publisher_lifecycle_mux);" in start
assert "g_physical_link_up.load(std::memory_order_relaxed)" in start
assert "g_scenario_txn.consume_start();" in start
assert start.index("g_physical_link_up.load") < start.index("g_running.store(true")
assert "START rejected: physical Ethernet LAN is DOWN" in start
link_handler = fw.split("void live_control_set_ethernet_link(", 1)[1].split(
    "void live_control_force_stop()", 1)[0]
assert "portENTER_CRITICAL(&g_publisher_lifecycle_mux);" in link_handler
assert "g_physical_link_up.store(up" in link_handler
assert "g_running.exchange(false" in link_handler
assert "g_start_request.store(false" in link_handler
assert "if (!was_running) return;" in link_handler
assert "SvStopCause::ethernet_link_down" in link_handler
# Link events may arrive after esp_eth_start() but before live_control_initialize().
# Never overwrite the event-owned PHY state during console initialization.
boot_init = fw.split("void live_control_initialize(", 1)[1].split(
    "void live_control_bind_publisher_task(", 1)[0]
assert "g_physical_link_up.store(false" not in boot_init
app_boot = eth.split('extern "C" void app_main(void)', 1)[1]
assert app_boot.index("esp_eth_start(eth_handle)") < app_boot.index("live_control_initialize(")
assert "SV-LAN-AUTONOMY-V1" in fw
assert "SESSION-LEASE" not in fw
assert "SV-LAN-AUTONOMY-V1" in device
assert "SVBANK BEGIN generation=" in fw and "SVBANK END generation=" in fw
assert "SVBANK BEGIN" in device and "SVBANK END" in device
assert "pendingBoardRows_.size() == 8" in device
assert "pendingBoardBoot_.compare(identity_.bootId" in device
assert "g_control_watchdog_ready.load" not in fw.split(
    "bool arm_control_lease_if_fresh()", 1)[1].split(
    "void uppercase_ascii(", 1)[0]
assert "SvStopCause::ethernet_link_down" in fw
assert 'return "ETHERNET_LINK_DOWN"' in fw
assert "const bool was_running = g_running.exchange(false" in fw
assert "ar_ptp_lab_request_link_down_stop();" in eth
assert "consecutive_canonical_tx_failures >= kTransportFailureStopThreshold" in eth
assert "g_stop_requested.store(true" in ptp
assert "void ar_ptp_lab_request_link_down_stop(void)" in facade
assert "ar_ptp_boot_supervisor_operator_override();" in facade
assert "g_link_fault_generation.fetch_add(" in facade
assert "admitted_link_generation !=" in facade
assert "g_role_control.request_stop_and_try_acquire()" in facade
assert "ptp_receiver_request_link_down_stop();" in facade
assert "ar_ptp_source_request_link_down_stop();" in facade
assert "void ptp_receiver_request_link_down_stop() noexcept" in receiver
assert "g_receiver_stop_requested.store(true" in receiver
assert "const bool was_up =" in eth
assert "if (was_up)" in eth
print("PASS: Studio/USB are control-plane only; explicit STOP and Ethernet fail-closed retained")
