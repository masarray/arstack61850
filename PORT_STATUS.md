# C++ Port Status

## Current milestone: Phase 1B

The C++20 port now includes the deterministic foundation, MMS Data values, IEC 61850 UTC
time, and the complete GOOSE wire codec from BER PDU through Ethernet/VLAN/process-bus
framing.

| Original C# area | C++ target | Status |
|---|---|---|
| ASN.1 BER primitives | `asn1` | Ported and tested |
| Ethernet, VLAN, process-bus header | `ethernet` | Ported and tested |
| Classic PCAP | `capture` | Ported and tested |
| GOOSE retransmission schedule | `goose/retransmission_schedule` | Ported and tested |
| IEC 61850 UTC time | `mms/utc_time` | Ported and tested |
| MMS Data / AllData | `mms/data_value`, `mms/data_codec` | Ported and tested |
| GOOSE PDU | `goose/pdu`, `goose/pdu_codec` | Ported and tested |
| GOOSE Ethernet frame | `goose/frame`, `goose/frame_codec` | Ported and tested |
| GOOSE runtime publisher/subscriber | Phase 1C | Pending |
| Sampled Values | Phase 1D | Pending |
| SCL and COMTRADE | Phase 2 | Pending |
| TPKT/COTP/ACSE/MMS association | Phase 3 | Pending |
| Reporting, control, and file service | Phase 4 | Pending |
| Simulator, CLI, and native UI | Phase 5 | Pending |

## Phase 1B validation

- C++20 GCC 14.2, warnings as errors: passed.
- C++20 Clang 17, warnings as errors: passed.
- Three CTest executables: core, MMS, and GOOSE.
- GOOSE PDU golden vector matches the C# encoder field order and BER representation.
- Complete VLAN-tagged GOOSE Ethernet frame has a byte-for-byte golden vector.
- PDU and frame round trips pass.
- Dataset entry-count mismatch, invalid UTC time, wrong outer tag, and wrong EtherType are rejected.
- No external C++ runtime dependency is required.

See `MIGRATION_CHECKLIST.md` for the complete roadmap and current checkboxes.

## Safety boundary

This milestone provides offline codecs only. It does not start a raw-Ethernet publisher,
subscribe to a network interface, perform MMS writes, or execute IED control.
