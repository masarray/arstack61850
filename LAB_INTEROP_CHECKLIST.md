# Isolated-Laboratory Interoperability Checklist

This checklist is intentionally receive-first and read-only. The Phase 1E tooling does
not open Npcap, transmit Ethernet frames, or operate an IED.

## 1. Safety boundary

- [ ] Use an isolated laboratory switch or an isolated VLAN with no route to a production substation LAN.
- [ ] Confirm the engineering laptop has no bridge, Internet sharing, or second adapter forwarding traffic.
- [ ] Begin with a mirror/SPAN port or passive TAP; do not connect an active publisher output first.
- [ ] Record IED manufacturer, model, firmware, configuration revision, MAC addresses, APPIDs, VLAN IDs, and sampling mode.
- [ ] Synchronize clocks only through the laboratory timing source approved for the test.

## 2. Receive-only capture

- [ ] Capture at least one stable GOOSE state and one GOOSE state transition.
- [ ] Capture Sampled Values across at least two counter wraps or ten seconds, whichever is longer.
- [ ] Preserve the original PCAP; do not edit it in Wireshark before hashing.
- [ ] Calculate SHA-256 and record the capture hash in the report template.
- [ ] Keep a copy of the SCL/CID/SCD file used to configure the streams.

## 3. Automated evidence check

Windows:

```powershell
./scripts/run-lab-check.ps1 -Pcap ./captures/device-session.pcap
```

Linux:

```bash
./scripts/run-lab-check.sh ./captures/device-session.pcap
```

Acceptance conditions:

- [ ] At least one GOOSE or Sampled Values packet is decoded.
- [ ] No process-bus packet is malformed.
- [ ] Every decoded GOOSE/SV frame re-encodes byte-for-byte to the captured frame.
- [ ] PCAP packet order, payload bytes, and microsecond-normalized timestamps round-trip.
- [ ] Stream identifiers, APPIDs, VLANs, and configuration revisions match the engineering files.

## 4. Protocol observations

GOOSE:

- [ ] `stNum` increases only on state changes.
- [ ] `sqNum` behavior and retransmission intervals match the configured profile.
- [ ] `timeAllowedToLive` remains adequate for observed arrival gaps.
- [ ] Dataset values match the expected state transition.

Sampled Values:

- [ ] `smpCnt` progression and wrap policy match the configured sampling mode.
- [ ] `confRev`, `smpSynch`, `smpRate`, and optional `refrTm` are consistent.
- [ ] Missing, duplicate, and out-of-order sample counts are zero or explained.
- [ ] `seqOfData` channel mapping and scaling are validated against SCL, not inferred from byte position alone.

## 5. Escalation and evidence retention

- [ ] Save the PCAP, JSON report, SHA-256, SCL files, device configuration export, and test notes together.
- [ ] Record every mismatch before changing the parser or normalizing captured bytes.
- [ ] Do not enable active GOOSE/SV transmission from this stack until the receive-only evidence passes and a separate approved transmit test plan exists.

## Current status

The synthetic C#-derived corpus is automated in CI. Physical IED interoperability
remains **not yet passed** until a real capture and completed report are committed or
otherwise archived as controlled evidence.
