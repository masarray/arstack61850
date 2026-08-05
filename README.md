# ARIEC61850 C++ Port

Incremental C++20 migration of the ARIEC61850 C# protocol stack. The C# implementation
remains the behavioral oracle until cross-language comparison and isolated-laboratory
interoperability are complete.

## Implemented

### Phase 0 — portable foundation

- CMake-based C++20 static library.
- BER TLV and primitive codecs.
- MAC, VLAN, Ethernet, IEC 61850 process-bus, and classic PCAP codecs.
- GOOSE retransmission schedule.

### Phase 1A — MMS data foundation

- IEC 61850 8-byte UTC time and quality byte.
- Recursive MMS Data values and AllData BER codec.
- C#-derived byte-for-byte golden vectors.

### Phase 1B — GOOSE wire codec

- Complete GOOSE PDU encode/decode.
- Ethernet/VLAN/process-bus GOOSE frame encode/decode.
- Dataset count and malformed-frame validation.

### Phase 1C — GOOSE runtime state machines

- Subscriber TTL and `stNum`/`sqNum` supervision.
- Duplicate, gap, regression, state-change, and wraparound classification.
- Deterministic publisher runtime with exponential retransmission scheduling.
- Offline encoded-frame output without raw-network activation.

### Phase 1D — Sampled Values core

- Sampled Values ASDU and multi-ASDU APDU models.
- Application-tag-0 BER encoder/decoder with strict `noASDU` validation.
- Ethernet/VLAN/process-bus frame codec for EtherType `0x88BA`.
- Optional `datSet`, `refrTm`, `smpRate`, and `smpMod` field handling.
- `smpCnt` continuity, normal-wrap, gap, duplicate, out-of-order, and restart tracking.
- Stream identity and configuration-revision supervision with aggregate statistics.
- IEC 61850 quality-word helper.
- Vendor-neutral raw `seqOfData` 32-bit word inspection with trailing-byte preservation.
- C#-derived byte-for-byte PDU and complete Ethernet-frame golden vectors.

No Phase 1 runtime opens Npcap, raw sockets, or a network interface.

## Build

### Windows — Visual Studio

```powershell
cmake -S . -B build -A x64 -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Linux — GCC or Clang

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Migration rule

1. Capture deterministic C# behavior and byte vectors.
2. Port the same behavior to C++.
3. Run strict multi-compiler builds and semantic state-machine tests.
4. Compare real or simulated PCAP/SCL evidence.
5. Enable active transport only after safety and interoperability gates pass.

See `MIGRATION_CHECKLIST.md` for the detailed progress ledger.
