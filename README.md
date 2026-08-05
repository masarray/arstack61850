# ARIEC61850 C++ Port

This directory is the incremental C++20 migration of the existing ARIEC61850 C# stack.
The C# implementation remains the behavioral reference until every migrated module has
cross-language golden-vector coverage.

## Implemented in Phase 0

- CMake-based C++20 static library.
- BER TLV reader/writer and primitive encoders.
- MAC address, VLAN, Ethernet frame, and IEC 61850 process-bus header codecs.
- Classic PCAP Ethernet reader/writer.
- GOOSE retransmission schedule.
- Standalone regression tests derived from the C# test suite.

## Build

### Windows: Visual Studio 2022

```powershell
cd cpp
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Windows/Linux: Ninja

```bash
cd cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Migration rule

Do not delete the C# module immediately after porting it. For each module:

1. Capture deterministic C# input/output vectors.
2. Implement the same public behavior in C++.
3. Run byte-for-byte codec comparisons and semantic state-machine tests.
4. Validate with laboratory PCAP/SCL evidence.
5. Only then switch the application boundary to C++.

Active control and publishing paths must remain disabled by default until the C++ path
has equivalent guardrails, cancellation, cleanup, and evidence handling.
