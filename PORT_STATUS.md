# C++ Port Status

## Repository audit

- Original implementation: C# / .NET 8.
- C# source files: 384.
- Approximate C# source size: 66,024 lines across libraries, applications, and tests.
- Core protocol library: 227 files / approximately 39,062 lines.
- Existing C# tests: 90 files / approximately 12,003 lines.
- Desktop UI: WPF/XAML and therefore requires a UI rewrite rather than direct translation.

## Phase 0 conversion delivered

The first port covers 17 C# source files representing approximately 934 lines of the
original deterministic foundation:

| Original area | C++ target | Status |
|---|---|---|
| `Asn1/Ber*` | `include/ariec61850/asn1`, `src/asn1` | Ported |
| `Ethernet/*` | `include/ariec61850/ethernet`, `src/ethernet` | Ported |
| `Capture/Pcap*` | `include/ariec61850/capture`, `src/capture` | Ported |
| `Goose/GooseRetransmissionSchedule` | `include/ariec61850/goose` | Ported |
| GOOSE PDU and MMS Data values | Phase 1 | Pending |
| Sampled Values codec and publisher core | Phase 1 | Pending |
| SCL and COMTRADE | Phase 2 | Pending |
| TPKT/COTP/ACSE/MMS client | Phase 3 | Pending |
| Reporting, control, file service | Phase 4 | Pending |
| Simulation, CLI parity, native desktop UI | Phase 5 | Pending |

## Validation performed

- GNU C++ 14.2, C++20, warnings as errors: passed.
- Clang 17, C++20, warnings as errors: passed.
- CTest regression executable: 8/8 checks passed under both compilers.
- No external C++ runtime dependency is required for Phase 0.

The sandbox did not contain the .NET SDK, so the original C# solution could not be rebuilt
inside this environment. The ported regression vectors were derived directly from the
repository source and its existing C# tests. Before merging into the main branch, run the
existing `dotnet test` suite on a machine with .NET 8 and preserve its output as the C#
baseline oracle.

## Safety boundary

This phase contains no active IED control and no raw-Ethernet publishing session. Active
paths should only be enabled after cross-language equivalence, cancellation, timeout,
association-loss cleanup, and isolated-lab interoperability have been demonstrated.
