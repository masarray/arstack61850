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
assert "live_control_ethernet_link_down_stop();" in eth
assert "SvStopCause::ethernet_link_down" in fw
assert 'return "ETHERNET_LINK_DOWN"' in fw
assert "const bool was_running = g_running.exchange(false" in fw
assert "ar_ptp_lab_request_link_down_stop();" in eth
assert "consecutive_canonical_tx_failures >= kTransportFailureStopThreshold" in eth
assert "g_stop_requested.store(true" in ptp
assert "void ar_ptp_lab_request_link_down_stop(void)" in ptp
print("PASS: Studio/USB are control-plane only; explicit STOP and Ethernet fail-closed retained")
