# ESP32-P4 PTP ↔ SMV continuous timeline — M2A

**Scope:** standards-oriented software phase steering and regression coverage, no relay available. This phase builds on M1 draft PR #153; the M2A PR targets the M1 branch. Do not merge to main independently.

## Architecture, one timing authority

- Hardware clock: ESP32-P4 Ethernet MAC IEEE1588 PTP time. LAB_SOURCE must already be active when the SV publisher starts. TIME_RECEIVER and MONITOR are NOT silently treated as a source clock for this M2A adapter.
- SV sample schedule: existing GPTimer 1 MHz rational-tick schedule and one canonical SV frame per period, retaining the verified firmware publisher/ISR; no second PTP stack and no duplicate scheduler.
- Off-realtime phase observer: one low-priority CPU0 task, notified at each publisher **sample-zero ISR**. It waits 250 ms then brackets one hardware PTP clock read with two GPTimer raw-timer readings. The timer lifetime is protected by a teardown mutex in the control plane, never an ISR mutex.
- Portable `SvPtpPhaseAuthority`: normalizes the phase of the GPTimer sample-zero *deadline* against the nearest PTP second; checks clock-read uncertainty, PTP monotonic continuity, epoch-generation continuity, gross steps and measured clock drift. On invalid evidence, reset/invalidate; no stale command persists.
- Bounded software PI: delivers at most ±400 ticks of 1 µs adjustment per one-second epoch. The ISR takes a lock-free correction command and distributes +/-1 tick over the **remaining intervals of the same epoch**, never resets `smpCnt`, catches up missed samples, or performs EMAC IOCTL/heap allocation/formatting/logging. A source/clock fault cancels a partially applied correction as soon as the background monitor detects it.
- Software telemetry continues to report measured scheduling lateness correctly by incorporating the accumulated ISR phase slews.
- Source PTP and canonical SV remain independently instrumented; AUTO `smpSynch` remains 0. **Software phase tracking is not proof of MAC egress timing or compliance.**

## Safety and admission

`CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL=n` **by default**. An explicit opt-in source-mode CI build exercises the new adapter, while the normal public firmware keeps the earlier scheduler. Features run only if:
1. Hardware EMAC timestamp support is compiled in (ESP-IDF 5.x / ESP32-P4).
2. LAB_SOURCE was started and its PTP clock is available **before** SMV START.
3. Active profile uses one canonical ASDU per SV frame with `sample_counter_modulus == publisher_rate_hz` (4000/4800 fps family only; this alone does NOT assert IEC 61869-9 compatibility).
4. Phase observer task and timer mutex were successfully created.
5. Monotonic GMTimer/EMAC bracket and consecutive source epochs are validated.

If any validation fails, no autonomous `smpSynch=1/2` claim is made. Do not enable this feature on energized power-system process buses.

## Portable test acceptance

`tests/test_sv_ptp_phase_authority.cpp` covers:

- 480 consecutive epochs with simulated +80 ppm independent GPTimer/EMAC clocks at both 4000 and 4800 fps; no phase jumps, bounded steering and tracking convergence.
- Exact distribution of corrections ±400, ±81, ±1, 0, 1, 73, 400 over one second; zero drift of the original rational base schedule.
- Reject source jumps, missed epochs, stale/long bracket reads, invalid inputs and rearm without previous integral commands.
- Relative GPTimer low-32-bit rollover after approximately 4295 seconds (71 minutes).
- Compile strict -Wall/-Wextra/-Wconversion/-Wsign-conversion/-Werror, GCC and Clang, plus MSVC /W4 /WX. ESP32-P4 SOURCE compile path exercised in dedicated CI matrix.

## Remaining gates and known limitations

1. **Physical measurement missing:** cross-clock PTP read uncertainty is modeled, but software tracking is based on the GPTimer alarm deadline. Actual SV sample instant / Ethernet MAC TX egress, contention, packetization latency, and long-term PTP output accuracy are NOT measured.
2. IEC/IEEE 61850-9-3 and IEC 61869-9 profile compliance must be independently assessed with full official profile text and a reference decoder/IED. The local clock class remains 248; an IED requiring a traceable precision GM may legitimately reject it.
3. A continuous full implementation must eventually account for PTP source quality, UTC/TAI provenance, startup/holdover/fault recovery, BMCA behavior, SCL-derived payload/ASDU variants, and physical TX timestamp evidence. No generic profile is assumed from an arbitrary SCL.
4. This lab-only feature is default OFF and does not certify absence of synchronization alarms. Release remains blocked until exact-head firmware CI and later physical benchmarks are reviewed.

## Validation links

- M1: https://github.com/masarray/arstack61850/pull/153
- M2 tracking issue: https://github.com/masarray/arstack61850/issues/154
- IEC/IEEE 61850-9-3: https://webstore.iec.ch/en/publication/24998
- IEC 61850-9-2+AMD1: https://webstore.iec.ch/en/publication/66549
- ESP-IDF GPTimer API: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/peripherals/gptimer.html
