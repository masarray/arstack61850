# PTP + SMV PCAP Wire Audit — M2B software evidence

Implemented in `ariec61850_ptp_sv_wire_audit`, using the **existing** ARIEC61850 Ethernet/PTP/SV codecs, PCAP reader and SampledValuesStreamSupervisor. No second protocol parser and no embedded realtime changes.

## Purpose

A combined packet-level audit to discover software-wire defects before an IED is available. It checks the engineered stream identity (APPID + svID), source MAC consistency, per-ASDU `smpCnt` continuity/wrap/gap/duplicate/out-of-order, `confRev` changes and counts of `smpSynch` 0/1/2, alongside PTP Announce/Sync/Follow_Up/PdelayResp visibility, selected domain and unexpected source changes.

Use a classic Ethernet **.pcap** file. If Wireshark captures .pcapng, export/save as .pcap first (the existing ARIEC61850 PcapReader is a classic-PCAP reader). Example after host C++ build:

```sh
./build/ariec61850_ptp_sv_wire_audit capture.pcap --sv-id "MU01/LLN0$SV1" --app-id 0x4000 --counter-modulus 4000 --ptp-domain 0 --json
```

`--counter-modulus` is required, not inferred from frame rate: one SV frame may contain multiple ASDUs and particular IEC 61869-9 profiles have distinct counter policies. Different APPID, svID or source MAC streams are never combined into one counter history.

## Tests and acceptance

`ariec61850_ptp_sv_wire_audit --self-test` writes and reads an in-memory PCAP using the existing PTP and SV encoders. It verifies both a CLEAN combined capture and a NEGATIVE capture. It checks Announce/Sync/Follow_Up, wrap 3→0 at an explicit modulus 4, ignores an interleaved unrelated svID, and detects an intentional missing-sample counter gap. This is a small software regression, not a conformance certificate.

- `svWireConsistent` means **only** the selected captured SV packets have no observed counter and identity errors. It does not infer correct profiles or absence of loss outside the capture window.
- `ptpPacketsVisible` means Announce/Sync and FollowUp for observed two-step traffic are visible in the selected domain. A passive PCAP **cannot attest PTP lock or GM accuracy**.
- `counterZeroHostCapturePhaseMinUs/MaxUs` are derived from **PCAP host receive timestamps**; do not interpret them as MAC hardware TX phase or legitimate microsecond precision.
- `ptpClockSynchronized`, `svPtpPhaseQualified`, and `iedAlarmFree` are `null` deliberately. Only a reference clock, hardware egress evidence and actual IED acceptance can resolve them.
- Exit status 0 means both wire consistency and essential PTP packet visibility were observed. It **never** means IEC/IEEE 61850-9-3 certification.

## Remaining PTP/SV engineering gates

1. Instrument actual ESP32-P4 canonical SV TX time against EMAC PTP (hardware timestamps / independent analyzer) without adding per-packet IOCTL latency.
2. Validate active selected SCL profile and IEEE/IEC profile requirements, including ASDU grouping and sample counter epoch.
3. Test sustained 4k/4.8k continuous phase steering and fault recovery on real silicon; keep the M2A experimental adapter default OFF until then.
4. Save PCAP + firmware commit + IED configuration for multi-vendor relay interoperability when equipment is available.

This tool is offline host-only. It does not affect 4000-fps embedded ISR behavior, native Qt Studio or public installer builds.
