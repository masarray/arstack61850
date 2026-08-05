# C++ Port Status

## Repository audit

- Original implementation: C# / .NET 8.
- C# source files: 384.
- Approximate C# source size: 66,024 lines across libraries, applications, and tests.
- Core protocol library: 227 files / approximately 39,062 lines.
- Existing C# tests: 90 files / approximately 12,003 lines.
- Desktop UI: WPF/XAML and therefore requires a UI rewrite rather than direct translation.

## Conversion delivered through Phase 1A

The port now covers the deterministic Phase 0 foundation plus the C# MMS data-value and UTC-time modules:

| Original area | C++ target | Status |
|---|---|---|
| `Asn1/Ber*` | `include/ariec61850/asn1`, `src/asn1` | Ported |
| `Ethernet/*` | `include/ariec61850/ethernet`, `src/ethernet` | Ported |
| `Capture/Pcap*` | `include/ariec61850/capture`, `src/capture` | Ported |
| `Goose/GooseRetransmissionSchedule` | `include/ariec61850/goose` | Ported |
| IEC 61850 UTC time and MMS Data values | `include/ariec61850/mms`, `src/mms` | Ported |
| GOOSE PDU | Phase 1B | Pending |
| Sampled Values codec and publisher core | Phase 1 | Pending |
| SCL and COMTRADE | Phase 2 | Pending |
| TPKT/COTP/ACSE/MMS client | Phase 3 | Pending |
| Reporting, control, file service | Phase 4 | Pending |
| Simulation, CLI parity, native desktop UI | Phase 5 | Pending |

## Validation performed

- GNU C++ 14.2, C++20, warnings as errors: passed.
- Clang 17, C++20, warnings as errors: passed.
- CTest regression executable: 12/12 checks passed under both compilers.
- C#-derived MMS golden vector: byte-for-byte match.
- GitHub Actions workflow added for GCC, Clang, and Windows MSVC.
- No external C++ runtime dependency is required through Phase 1A.

The sandbox did not contain the .NET SDK, so the original C# solution could not be rebuilt
inside this environment. The ported regression vectors were derived directly from the
repository source and its existing C# tests. Before merging into the main branch, run the
existing `dotnet test` suite on a machine with .NET 8 and preserve its output as the C#
baseline oracle.

## Safety boundary

This phase contains no active IED control and no raw-Ethernet publishing session. Active
paths should only be enabled after cross-language equivalence, cancellation, timeout,
association-loss cleanup, and isolated-lab interoperability have been demonstrated.
