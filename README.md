# ARIEC61850 C++ Port

Incremental C++20 migration of the ARIEC61850 C# stack. The C# implementation remains
the behavioral oracle while each module gains byte-for-byte vectors and malformed-input
tests.

## Implemented through Phase 1B

- BER primitives, Ethernet/VLAN/process-bus framing, PCAP, and GOOSE retransmission timing.
- IEC 61850 UTC time and recursive MMS Data / AllData codecs.
- GOOSE PDU encoder/decoder for `gocbRef`, `timeAllowedToLive`, `datSet`, `goID`, `t`,
  `stNum`, `sqNum`, `test`, `confRev`, `ndsCom`, `numDatSetEntries`, and `allData`.
- Complete GOOSE Ethernet frame encoder/decoder with optional VLAN.
- GCC, Clang, and MSVC GitHub Actions coverage.
- C#-compatible MMS and GOOSE golden vectors.

See [`MIGRATION_CHECKLIST.md`](MIGRATION_CHECKLIST.md) for detailed progress.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For Visual Studio multi-configuration builds:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Migration rule

A module is complete only after C++ tests, C#-versus-C++ vectors, malformed-input coverage,
and relevant PCAP or laboratory evidence. Active publishing and control remain disabled
until their state-machine safety gates pass.
