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


## ARStack Studio one-click evidence V2 (SV + PTP)

In **Advanced → Device**, use **Capture SV + PTP** to write the same-timeline
L2 SV (0x88BA) and PTP (0x88F7) traffic, including one or two VLAN tags.
This does not send any device command. The actual monitor adapter must receive
both multicast groups: a TAP/SPAN that exposes SV but not PTP is not sufficient
to correlate `smpSynch`.

The exported bundle now contains:

- `*.pcap`: lossless on-host packet data including SV and any observed PTP.
- `*_report.json`: frame counts, PTP message categories, per-channel wire
  min/max/mean/RMS/optional phasor estimate, quality word counts, and status
  `CAPTURED_WITH_ANOMALIES` when loss, duplicates or malformed bytes are found.
- `*_events.csv`: compact forensic events, one row per discontinuity,
  duplicate, host capture timestamp gap (>1 ms), `smpSynch` change, and
  observed PTP Sync/Follow_Up/Announce frame.
- `*_seconds.csv`: entire-window per-second packet/loss summaries.
- `*_samples.csv`: full 4I+4V values for 30/120s, 1-in-20 values for 3600s.

**Upload `*_report.json` and `*_events.csv` first**. Include `*_seconds.csv`
for rate analysis; attach the PCAP only when inspecting samples, BER, VLAN,
transmit source, or timestamps. The large `*_samples.csv` is not required
for first-stage triage because the report calculates eight-channel statistics
directly from the raw packet decoder.

### What a small packet gap means

An observed transition such as `smpCnt=728 → 789` indicates 60 *missing
captured sequence numbers*. It does **not** distinguish publisher frame loss,
Ethernet switch/SPAN loss, NIC/driver queue overflow, or capture startup.
A PC host gap (>1 ms) is not a hardware egress interval. Correlate with ESP32-P4
canonical missed/fail counters recorded at both beginning and end, NIC/dropcap
drop counters, and (where available) a hardware-timestamp TAP.

### Synchronization interpretation

`smpSynch=0` means no qualified sync advertised; `smpSynch=1` is a local
clock claim and **is not evidence of external GM lock**. The initial Studio
telemetry snapshot cannot describe a 120-second PTP transition; correlate
`SMPSYNCH_CHANGE` events with live PTP Sync / Follow_Up / Announce packets,
firmware PTP phase-acquisition logs and SOURCE qualification. A PTP capture
alone still cannot demonstrate coherent SV/PTP phase, source traceability,
or legitimate use of `smpSynch=2`. Never call such a capture a hardware PASS
without independent timing and source evidence.

The per-channel phasor estimates assume the GUI frequency recorded at capture
start and a 4000 fps, 4000-count basis. They are not a calibrated magnitude
contract or proof that operator inputs stayed unchanged during the capture.
