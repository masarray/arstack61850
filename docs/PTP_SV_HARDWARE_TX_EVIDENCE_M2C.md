# M2C — sparse hardware SV TX timestamp evidence (ESP32-P4, software build scope)

## Why this is an actual milestone

M2A tracks the relationship between the 1 MHz GPTimer **sample-zero alarm deadline** and the ESP32-P4 EMAC PTP clock. M2B audits off-device wire frames. Neither measures when a canonical SV actually exits the Ethernet MAC. M2C adds an **opt-in descriptor timestamp on the actual canonical Sampled Values frame**, not on an unrelated diagnostic packet.

Verified pinned driver API reference:
- ESP-IDF **v5.5.4** `components/esp_eth/src/mac/esp_eth_mac_esp.c`: `emac_esp32_transmit_ctrl_vargs()` forwards the supplied control pointer to `emac_esp_dma_transmit_frame_ext()`.
- ESP-IDF **v5.5.4** `components/esp_eth/src/mac/esp_eth_mac_esp_dma.c`: the extended TX path waits for the final DMA TX descriptor to return a timestamp, and returns zero seconds/nanoseconds on timeout. **It busy-waits**; using it on every 4000/4800-fps frame would add unacceptable work to the hard timing path.
- https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_eth/src/mac/esp_eth_mac_esp.c
- https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_eth/src/mac/esp_eth_mac_esp_dma.c

## Conservative implementation

`CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL`: **default OFF**. Requires the existing experimental PTP phase authority and *disabled* broadcast mirror/diagnostic probes, to avoid mixing lab-only traffic with the measured canonical stream.

Only while a PTP SOURCE was started before SV START, the sample profile has exactly one canonical ASDU/frame and the counter modulus equals the frame rate:

1. Count frames with `smpCnt=0`. Every **16th** such frame, request one hardware TX descriptor timestamp by `esp_eth_transmit_ctrl_vargs(eth_handle, &timestamp, 2U, buffer, bytes)`.
2. All other canonical frames use the **unchanged** `esp_eth_transmit()` baseline. No additional IOCTL, log, heap allocation, lock or timestamp polling inside the GPTimer ISR. The foreground publisher briefly polls the selected DMA descriptor **once per 16 seconds**; this could still induce jitter/missed slots, so it is disabled for actual process-bus operation.
3. The portable `SvTxEgressEvidence` collector takes an explicit record: `smpCnt`, epoch generation, 32-bit GPTimer *scheduled deadline*, hardware PTP seconds/nanoseconds, software TX submit duration, canonical TX outcome, recent missed-slot and generation checks. A stale epoch, missing descriptor timestamp, failed TX or invalid nanoseconds is **NOT MEASURED**, never silently substituted with host time.
4. Accepted samples calculate signed `PTP-egress-phase_ns` with respect to the nearest PTP whole-second boundary (positive after boundary, negative before). The collector retains bounded min/mean/max and failure counts, no allocation. One 1 Hz telemetry task formats a diagnostic log **only when a sparse observation changes**.
5. Neither PTP clock quality nor IED receiver lock is inferred. `smpSynch` remains fail-closed; no automatic promotion to 1/2.

## Evidence interpretation (important)

- Hardware MAC egress timestamp is genuine descriptor evidence **only if the EMAC driver returns nonzero, valid seconds and nanoseconds for the canonical SV EtherType 0x88BA**. Some hardware timestamp filters may accept only recognized PTP frames; in that case the diagnostic records `no_hw_ts`, not invented nanosecond precision.
- The source clock is laboratory clockClass **248**, not certified traceable UTC/GNSS. Hardware timestamping does **not** make it an IEC/IEEE 61850-9-3 qualified grandmaster.
- `last_alarm_ticks` is the most recent sample-zero GPTimer **deadline**, **not converted into an absolute PTP timestamp**. Conversion would need a continuously valid cross-clock mapping. A near-zero egress phase is only one piece of evidence; it does not prove every sample deadline meets timing requirements, nor resolve exact-second ambiguity after an epoch slip.
- `submit_max_us` measures CPU-side blocking while transmitting the selected frame; it is not MAC timestamp uncertainty, P99 of normal frames, or link propagation delay.
- To avoid silent interpretation errors, no `phaseQualified` or `IEDAlarmFree` status is emitted. A high transmit duration or missed slots during the probe may itself show the probe is too intrusive.

## Software-only acceptance before physical devices

- Portable `tests/test_sv_tx_egress_evidence.cpp`: positive and negative phase, valid exact-second timestamps, timestamp missing/overflow, missing canonical frame, mismatched epoch, failed TX, wraparound of low-32-bit GPTimer deadline, and bounded aggregate state.
- One explicit **Draft checkpoint** compiles both portable phase and hardware-egress evidence tests using `-Werror` and compiles ESP32-P4 SOURCE firmware with `CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL=y`, `CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL=y` and broadcast mirror disabled. This validates source against the pinned toolchain, not correct silicon timestamp behavior.
- No new dedicated always-on workflow. Standard ready-PR PTP core CI also compiles the portable evidence test when PR is promoted.
- Baseline firmware, Windows native ARStack Studio and public release remain unchanged.

## Remaining hardware gates

1. Test on ESP32-P4 pre-v3 silicon and inspect real `no_hw_ts`, phase jitter, and induced sample loss against a hardware-timestamped capture.
2. If SV EtherType timestamps are absent, audit Espressif descriptor timestamp filtering before proposing a driver-level patch; **do not lie with software send-time placeholders**.
3. Correlate sample deadline with EMAC PTP time under controlled clock steps and holdover; measure 4k/4.8k receiver behavior, especially IED synch alarms and GM acceptance.
4. Remove laboratory broadcast mirror and diagnostics from the bench profile; keep the normal production release unchanged until interop evidence exists.
