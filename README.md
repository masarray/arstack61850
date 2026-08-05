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

- Subscriber TTL supervision and one-shot expiry events.
- `stNum`/`sqNum` classification: first, retransmission, duplicate, gap, regression,
  state change, state jump, and state regression.
- 32-bit sequence and state wraparound handling.
- Expected control-block, dataset, and configuration-revision identity checks.
- Detection of data changes without a matching `stNum` increment.
- Publisher session matching the C# counter semantics.
- Deterministic publisher runtime with exponential retransmission scheduling.
- State timestamps remain stable during retransmission and change only on a state event.

The publisher runtime returns encoded frames to its caller. It does not open Npcap,
raw sockets, or a network interface.

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
