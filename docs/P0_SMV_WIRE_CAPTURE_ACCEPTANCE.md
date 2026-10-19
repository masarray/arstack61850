# P0-A independent SMV wire capture gate (not a bench PASS)

This runbook is evidence collection guidance, not a claim that a board was exercised.
Never infer physical SV acceptance from Qt status, synthetic regression PCAP or GitHub firmware CI.

## Verify identity before measurement
- Check the Studio semantic `IDENTIFY` line: firmware build ID, boot ID, device MAC,
  `SV-CONFIG-TXN-V1` capability and the expected board/USB identity.
- Record the `CFG BEGIN`, `CFG COMMITTED`, `CFG READBACK` and
  `START accepted: profile armed token=… hash=…` serial sequence.
- `CFG COMMITTED` alone is not START success; `CFG READBACK` alone is not START.
  Confirm the board's `SHOW` and inspect the **independent captured wire**.
- Do not enable `smpSynch=1/2` by hand. Verify PTP SOURCE announce/sync/follow-up,
  actual discipline and MAC/port domain trace before qualifying synchronized output.

## Independent PCAP collection

Capture Ethernet interface frames independently from the Studio USB serial controller,
with full snapshots (`-s 0`) and loss statistics. The SPAN or TAP and OS capture path
must not drop SV frames under load; a capture loss makes the continuity gate indeterminate.

Example using Wireshark's Dumpcap (set actual interface and file path):

```sh
dumpcap -i YOUR_MONITOR_INTERFACE -s 0 -a duration:125 -w sv_120s.pcap
```

**Important**: Dumpcap usually writes PCAPNG, not classic PCAP. The offline audit
intentionally accepts only *classic Ethernet PCAP* and fails closed on unsupported
formats. Convert using Wireshark's editcap:

```sh
editcap -F pcap sv_120s.pcap sv_120s_classic.pcap
```

Record capture packet drops, board TX counters, and host NIC load separately.

## Offline automation (read-only)

```sh
python3 tools/smv_wire_capture_audit.py sv_120s_classic.pcap \
  --appid 0x4000 --sv-id ARSTACK_SV01 --wrap 4000 \
  --minimum-seconds 120 --fps 4000 --signal-frequency-hz 50 \
  --expected-json scenario_4i4v.json
```

For the endurance run, change to `--minimum-seconds 3600` and use a captured
window longer than 3600 seconds. Any gap/duplicate in the one-ASDU stream, corrupt
frame, unrecognized stream, wrong duration or average FPS outside ±2% returns
a nonzero exit code. The parser computes independent fundamental RMS and phase
for **all eight wire channels** using `smpCnt` and measured frame data. It checks
against the *independently recorded normalized integer wire counts*, not GUI
floating-point display values.

Example *input schema* (fill values from the operator's current scenario rather
than copying unrelated nominal defaults):

```json
{
  "ct_shaping_enabled": false,
  "channels": {
    "IA": {"rms_counts": 1000, "phase_degrees": -30},
    "IB": {"rms_counts": 1000, "phase_degrees": -150},
    "IC": {"rms_counts": 1000, "phase_degrees": 90},
    "IN": {"rms_counts": 0, "phase_degrees": 0},
    "UA": {"rms_counts": 5774, "phase_degrees": 0},
    "UB": {"rms_counts": 5774, "phase_degrees": -120},
    "UC": {"rms_counts": 5774, "phase_degrees": 120},
    "UN": {"rms_counts": 0, "phase_degrees": 0}
  }
}
```

Those counts above are **illustrative only**: actual counts depend on the
profile's currentCountsPerAmp and voltageCountsPerVolt scales. Never use
example counts as acceptance truth. For nonlinear CT saturation, compare raw
waveform and configured nonlinear distortion with a dedicated evaluator;
this fundamental equivalence mode deliberately rejects the scenario
(`ct_shaping_enabled=true`).

The status `PASS_PCAPPARSER_ONLY` is **not** physical acceptance. It means
only that the selected PCAP passed this parser's specific continuity,
frame-rate, field and optional setpoint checks. Before release, separately
prove SV MAC/VLAN/APPID/confRev and quality semantics, independent PTP
measurements, PHY/ISR counters, CPU headroom, stack high-water marks,
3600-second stability and ten replug/recovery cycles.

## Self-tests

```sh
python3 -m unittest discover -s tests -p test_smv_wire_capture_audit.py -v
```

Synthetic 50-Hz waveforms in these tests **do not** substitute for physical
Ethernet measurements.
