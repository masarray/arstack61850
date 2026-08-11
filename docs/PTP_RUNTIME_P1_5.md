# PTP-P1.5 Runtime Completion

PTP-P1.5 turns the PTP-P1 wire engine into a reusable runtime/control layer while preserving the ESP32-P4 hardware timestamp path.

This remains a laboratory timing companion for troubleshooting and interoperability. It is not a GPS-backed clock, a certified grandmaster, or a formal IEC/IEEE 61850-9-3 conformance claim.

## Portable runtime

`include/ariec61850/time_sync/ptp_runtime.hpp` provides a platform-neutral PTP publisher runtime on top of the existing codec.

The runtime owns:

- publisher profile/configuration
- ClockIdentity parse/format and EUI-64-style MAC derivation
- lifecycle (`start`, `stop`, stopped-only `reconfigure`)
- Announce/Sync due scheduling
- per-message sequence counters
- Announce, Sync, Follow_Up, Pdelay_Resp and Pdelay_Resp_Follow_Up frame preparation
- publisher status and send/error accounting

The runtime intentionally does not own:

- an operating-system thread
- a NIC or transport driver
- a PTP clock servo
- absolute UTC traceability
- lock/holdover state
- `smpSynch` promotion

Those boundaries keep the core portable across desktop, embedded and analyzer applications.

## Portable publisher options

`PtpPublisherOptions` covers the relevant ARIEC61850 publisher profile:

- `transport_specific`
- `domain_number`
- source MAC
- optional VLAN ID / PCP
- ClockIdentity and port number
- Announce / Sync intervals
- two-step policy
- peer-delay response policy
- priority1 / priority2
- clockClass / clockAccuracy
- offsetScaledLogVariance
- timeSource
- currentUtcOffset
- stepsRemoved

Reconfiguration is deliberately rejected while the runtime is running. A control plane must perform a safe `stop -> reconfigure -> start` transaction instead of mutating live publisher state.

## Publisher status

`PtpPublisherStatus` exposes:

- running state
- start time
- last send time
- Announce sent
- Sync sent
- Follow_Up sent
- peer-delay response frames sent
- last runtime error

This is the reusable status contract for future GUI and analyzer integrations.

## ESP32-P4 hardware adapter

`embedded/esp32p4_smv_injector/main/ptp_lab_task.cpp` is now an adapter around `PtpPublisherRuntime` rather than a second PTP runtime implementation.

The adapter remains responsible only for ESP-specific work:

- FreeRTOS task scheduling on CPU0, separate from the realtime SV task
- ESP-IDF Ethernet transport
- enabling the ESP32-P4 IEEE1588 hardware clock
- hardware TX descriptor timestamp capture for Sync and Pdelay_Resp
- hardware RX descriptor timestamp capture for Pdelay_Req
- RX queueing and task notification

Two-step timestamp evidence remains:

- Sync carries zero originTimestamp
- actual Sync hardware TX timestamp becomes Follow_Up preciseOriginTimestamp
- Pdelay_Req hardware RX timestamp becomes Pdelay_Resp requestReceiptTimestamp (`t2`)
- Pdelay_Resp hardware TX timestamp becomes Pdelay_Resp_Follow_Up responseOriginTimestamp (`t3`)

The hardware implementation therefore remains stronger than the original desktop ARIEC61850 software-timestamped publisher.

## ESP runtime control bridge

The C-compatible adapter API is designed so a future GUI, USB/RPC control task, or embedded application can control PTP without recompiling the PTP core.

Available operations:

- `ar_ptp_lab_get_default_config`
- `ar_ptp_lab_configure`
- `ar_ptp_lab_get_config`
- `ar_ptp_lab_start`
- `ar_ptp_lab_stop`
- `ar_ptp_lab_is_running`
- `ar_ptp_lab_get_status`

Runtime configuration is accepted only while PTP is stopped. The live status bridge exposes lock-free counters for Announce, Sync, Follow_Up, peer-delay response frames and TX failures.

Kconfig remains the firmware default profile. A runtime configuration override can replace it before the task is started.

## ESP Kconfig defaults

The ESP32-P4 profile now exposes:

- PTP enable
- domain
- transportSpecific
- optional ClockIdentity override
- port number
- optional VLAN / VID / PCP
- Announce interval
- Sync interval
- Pdelay response enable
- priority1 / priority2
- clockClass
- clockAccuracy
- offsetScaledLogVariance
- timeSource
- currentUtcOffset

Defaults remain intentionally conservative: clockClass 248, clockAccuracy Unknown, variance `0xFFFF`, timeSource Internal Oscillator.

## Validation

PTP-P1.5 has dedicated portable regression coverage for:

- ClockIdentity parsing/formatting
- profile validation
- stopped-only reconfiguration
- due scheduling
- per-message sequence behavior
- status accounting
- two-step Sync/Follow_Up
- one-step suppression of Follow_Up
- Pdelay request sequence/log interval preservation
- supplied `t2` / `t3` timestamp placement

The strict PTP profile is built on GCC, Clang and MSVC with warnings treated as errors. The ESP workflow builds a non-default PTP profile on ESP-IDF 5.5.4, while the normal ESP32-P4 SMV firmware build verifies that PTP remains default-off and does not regress the injector.

## Safety boundary

PTP-P1.5 does **not** change Sampled Values synchronization claims. The generic SV default remains `smpSynch=0`, and the ESP injector must not promote it merely because PTP frames are being transmitted or observed.

Promotion to `smpSynch=1/2` belongs to PTP-P2 after measured clock discipline, offset/delay qualification, lock/holdover state, and explicit policy evidence exist.
