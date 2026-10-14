# M2F — fail-closed SOURCE freshness and truthful PTP time-properties

## Security / interoperability issues

1. Before this checkpoint the previously introduced SOURCE readiness gate was fresh-send-aware but NOT age-aware. A FreeRTOS SOURCE task that stalled after a successful Announce+Sync could leave `g_ptp_ready=true` indefinitely. This is especially dangerous because the SV phase observer queries SOURCE health on its own task.
2. Successful `ETH_MAC_ESP_CMD_S_PTP_TIME` from an apparently plausible RTC was treated as sufficient to assert `ptpTimescale` in **all** Announce/Sync/FollowUp messages—even when `CONFIG_AR_PTP_MANUAL_UTC_OFFSET_VALID` was disabled. An arbitrary RTC with an unverified TAI−UTC offset is not a proven PTP timescale.

## Implemented fail-closed checks

- Reusable zero-allocation `PtpSourceEvidenceFreshness::fresh(now, lastAnnounce, lastSyncPair, timeout)` demands two positive monotonic timestamps within the configured bounded age. In default 1Hz power-profile SOURCE this is 3 seconds; an explicit nonstandard bench cadence uses 3 × max(Announce,Sync) interval. Zero timestamps, negative/zero timeout, backwards monotonic time, stale Announce or stale Sync all fail.
- The ESP32-P4 SOURCE control plane updates the two success timestamps only after complete actual wire sends. A failure clears both timestamps. `ar_ptp_lab_is_running()`, SOURCE status, startup readiness and `ar_ptp_lab_source_generation()` use the age-validated status. If the task was suspended long enough to expire, it invalidates the prior gate BEFORE attempting new sends, so a complete fresh core cycle is necessary for requalification and a new SOURCE generation.
- `conservative_lab_time_properties(hw_tai_seeded, tai_utc_offset_verified)` sets PTP timescale AND currentUtcOffsetValid only if both pieces of evidence exist. If either is missing, both flags remain false; timeTraceable and frequencyTraceable remain false in all cases. The lab clock continues to identify itself as clockClass 248, unknown accuracy, internal oscillator.
- Portable tests cover zero, stale, backwards, boundary-expired and fresh monotonic evidence and check actual raw Announce Ethernet flag bytes for unverified (0x0000) versus manually verified seeded timescale (0x000c). The already opt-in single CI checkpoint runs C++ PTP and SV regressions and the ESP32-P4 SOURCE firmware build with ESP-IDF v5.5.4.

## Limitations and measured-evidence boundary

- These checks do not establish IEEE/IEC 61850-9-3 certification, IEEE1588 traceability or a globally synchronized grandmaster. In default mode the locally seeded LAB_SOURCE explicitly claims **unknown** PTP timescale; strict IEDs can reject that clock. This is preferable to spoofing quality and missing an unsafe time alarm.
- Source freshness only measures recent *successful sends*; it is not proof that IEDs received, selected or locked to SOURCE. SOURCE scheduler jitter, SV actual TX egress phase, and IED sync alarms still require silicon/receiver evidence.
- No change to the default-off experimental SV phase servo or public firmware release. No UI/installer changes.
