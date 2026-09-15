# ARStack61850 — Real-Time IEC 61850 Instrumentation Stack

ARStack61850 is the real-time, hardware-aware evolution of ARIEC61850.

> **ARIEC61850 understands IEC 61850. ARStack61850 measures and executes IEC 61850 in real time.**

The portable C++20 stack remains rooted in the ARIEC61850 behavioral model, but the project north star is now broader: build an open, deterministic IEC 61850 instrumentation platform for hardware-timestamped Sampled Values/GOOSE/PTP injection, analysis, commissioning and real-time closed-loop testing.

## Public release — SMV Injector v0.1.0

**Status: STABLE PUBLIC RELEASE · 2026-09-15**

ARStack Studio / SMV Injector v0.1.0 is the first public production milestone of the ESP32-P4 Sampled Values track. It packages the native Windows operator application, the accepted ESP32-P4 firmware, recovery tooling, and the validated 4I+4V reference workflow.

**[Download SMV Injector v0.1.0](https://github.com/masarray/arstack61850/releases/tag/v0.1.0)**

Public P0 profile:

- 4 current + 4 voltage channels;
- 4000 frames/s;
- 1 ASDU per Ethernet frame;
- 64-byte sample payload;
- `smpCnt` modulus 4000;
- `smpSynch=0` until separately measured disciplined-clock evidence exists.

Accepted physical evidence includes 10/10 Start/Stop cycles, live engineering edits, an uninterrupted 3600-second retained run at 4000 fps with `missed=0`, `txFailures=0`, `healthReconnects=0`, and `automaticStarts=0`, plus an independent SV capture spot-check with continuous `smpCnt` including 3999 -> 0 wraps.

Release source target: `9c7fc7300220db4643e5643081240b955cfe12df`. Accepted binary build head: `d9b5b6848415c7e6d1c52ec929e57c66b608058d`. See [`docs/SMV_STUDIO_V0.1.0_PUBLIC_RELEASE.md`](docs/SMV_STUDIO_V0.1.0_PUBLIC_RELEASE.md) for immutable release provenance, hashes, scope, and the remaining non-blocking research boundary.

## Start here

For a new engineer or AI development thread, read these before making architecture changes:

1. [`NORTH_STAR.md`](NORTH_STAR.md) — product thesis, standards, hardware roadmap and definition of success.
2. [`docs/SMV_STUDIO_V0.1.0_PUBLIC_RELEASE.md`](docs/SMV_STUDIO_V0.1.0_PUBLIC_RELEASE.md) — current public SMV product milestone, release provenance and validated boundary.
3. [`PROJECT_HANDOFF.md`](PROJECT_HANDOFF.md) — current repository handoff and separation between released SMV work and active follow-on tracks.
4. [`apps/arstack_studio/README.md`](apps/arstack_studio/README.md) — operator workflow, firmware recovery and Windows distribution details.
5. [`docs/ONLINE_MODEL_CONNECT_DECISION.md`](docs/ONLINE_MODEL_CONNECT_DECISION.md) — short decision contract separating full live discovery from SCL-assisted online connect.
6. [`LIVE_DISCOVERY_PROFILE.md`](LIVE_DISCOVERY_PROFILE.md) — detailed live-discovery and online-model interoperability boundary.
7. [`embedded/esp32p4_smv_injector/ARCHITECTURE.md`](embedded/esp32p4_smv_injector/ARCHITECTURE.md) — embedded real-time architecture boundaries.
8. [`docs/GLOBAL_BENCHMARK.md`](docs/GLOBAL_BENCHMARK.md) — vendor-neutral standards/industry research behind the direction.

The project deliberately separates **wire facts**, **configured expectations**, **profile claims**, and **synchronization evidence**. A packet that decodes in a host capture tool is not, by itself, a conformance or timing claim.

## Current SMV product milestone

The P0 ESP32-P4 Sampled Values product track is **released and public** as `v0.1.0`. The stable GitHub release points to production merge `9c7fc7300220db4643e5643081240b955cfe12df` and publishes a Windows installer, portable package, accepted firmware image, manifest and release checksums.

Physical acceptance retained for the public milestone:

- 10/10 Start/Stop cycles passed;
- uninterrupted 3600-second retained run held 4000 fps;
- `missed=0`, `txFailures=0`, `healthReconnects=0`, `automaticStarts=0`;
- independent capture confirmed the expected SV profile, one ASDU, 64-byte `seqData`, continuous `smpCnt` and 3999 -> 0 wraps;
- `smpSynch=0` remains the truthful synchronization state.

The next process-bus research gate is measured hardware timestamp/PTP discipline and broader profile support. Those are follow-on milestones and are **not** retroactively claimed by v0.1.0.
## Lineage and portable core

The original work began as an incremental C++20 migration of the ARIEC61850 C# protocol stack. The C# implementation remains a behavioral oracle where cross-language equivalence is still useful, while ARStack-specific work adds deterministic RTOS/hardware execution and evidence that a normal desktop stack cannot provide.

The project is **not yet a complete IEC 61850 replacement stack**. Current
Public Alpha work focuses on a useful read-only MMS engineering client, process
bus codecs/runtime foundations, reproducible cross-language evidence, and an
MCU-safe protocol boundary.

## Implemented

### Phase 0 — portable foundation

- CMake-based C++20 static library.
- BER and MMS data/UTC-time codecs.
- Ethernet/VLAN process-bus framing and classic PCAP support.
- GOOSE and Sampled Values codecs plus offline/runtime supervision foundations.
- SCL read-only parsing and COMTRADE CFG/DAT support for host engineering tools.
- RFC 1006 TPKT, COTP, ISO Session, Presentation, ACSE, and MMS Initiate.
- Confirmed MMS GetNameList, GetVariableAccessAttributes, Read, and Write codecs.
- Read-only MMS FileDirectory/FileOpen/FileRead/FileClose codecs and bounded streaming runtime.
- DataSet directory, InformationReport, RCB state, and report-subscription foundations.

### Phase 1A–1E — deterministic process-bus core

- IEC 61850 UTC time and recursive MMS Data/AllData codec.
- Complete GOOSE PDU/frame codecs and offline publisher/subscriber supervision.
- Sampled Values ASDU, multi-ASDU PDU, Ethernet frame, payload, quality, counter, and stream supervision.
- C#-derived synthetic PCAP equivalence evidence.
- Sanitizer, deterministic mutation-smoke, and LLVM libFuzzer coverage.
- Receive-only isolated-laboratory tooling.

A codec existing in the library does **not** mean the corresponding mutating
operation is enabled in live Phase 4C discovery.

### Phase 2A — SCL foundation

- Bounded, dependency-free XML/SCL reader.
- IED, DataSet, FCDA, GOOSE, Sampled Values, report-control, and communication mapping.
- SCL edition detection, type-template resolution, DataSet binding, diagnostics, and SCL/XML fuzzing.

### Phase 2B — COMTRADE foundation

- CFG parsing for IEEE C37.111-style 1991/1999/2013 records.
- ASCII, BINARY, BINARY32, and FLOAT32 DAT readers.
- Analog engineering scaling (`a * raw + b`) and packed digital status decoding.
- Timestamp multiplier and multi-rate fallback scheduling.
- Default `Va/Vb/Vc/Vn/Ia/Ib/Ic/In` channel mapping.
- Deterministic SCL Sampled Values to COMTRADE channel binding.
- Read-only `ariec61850_comtrade_inspect` human/JSON CLI.
- C#-derived ASCII and binary fixtures, mutation smoke, sanitizer, and libFuzzer corpus.

### Phase 3A — MMS transport foundation

- RFC 1006 TPKT frame encode/decode with strict version, reserved-octet, and length validation.
- Incremental TPKT stream framing for fragmented and coalesced TCP byte streams.
- COTP Connection Request, Connection Confirm, Data, Disconnect Request, and Error TPDU decoding.
- Default C#-compatible CR vector, TPDU-size negotiation, and TSAP selector mirroring.
- COTP Data segmentation by negotiated TPDU size and bounded EOT reassembly.
- Fragment-count, byte-count, and empty non-final fragment abuse guards.
- C#-derived golden vectors, deterministic mutation smoke, sanitizer, and libFuzzer coverage.

### Phase 3B — MMS association codecs

- Bounded ISO Session Connect/Accept SPDU and data-transfer profile codecs.
- Presentation CP/CPA context definitions, negotiation results, selectors, and PDV routing.
- ACSE and MMS context negotiation using BER transfer syntax.
- Structured ACSE AARQ/AARE application context, AP-title, AE-qualifier, result, and diagnostic parsing.
- EXTERNAL user-information handling with MMS Initiate retained as an opaque Phase 3C payload.
- Deterministic association-response construction with session and context mirroring.
- Byte-exact C# request/response golden vectors, mutation smoke, sanitizer, and libFuzzer coverage.

### Phase 4C — live read-only MMS discovery

`ariec61850_live_discover` currently provides:

- built-in Windows/POSIX TCP transport;
- MMS association through the implemented OSI stack;
- domain and variable GetNameList discovery;
- named-variable-list/DataSet inventory;
- bounded variable-type probes;
- bounded RCB read evidence;
- LD/LN/DO/DA live-model projection;
- IED identity inference;
- CDC/type heuristics compatible with the C# oracle;
- GOOSE/SV/setting-group/log control-block name inventory;
- dynamic RCB `Bound` / `Unbound` / `NotRead` / `ReadFailed` evidence;
- read-only RCB candidate planning and conservative operational availability.

The live discovery surface sends only read-only services such as GetNameList,
GetVariableAccessAttributes, GetNamedVariableListAttributes and Read. It does
**not** authorize or perform Write, control, GI, RCB reservation/enable,
dynamic DataSet mutation, or file-service mutation.

### Phase 4E - MMS file transfer client

`ariec61850_file_transfer` adds an explicit read-only file-service surface:

- root/nested FileDirectory with bounded continuation;
- streaming FileOpen/FileRead/FileClose download to a caller-owned sink;
- full signed Integer32 FRSM support;
- exact Confirmed-Error evidence and bounded diagnostics;
- best-effort FileClose that preserves the primary failure; and
- a single oracle-matched rooted-backslash fallback for file-non-existent.

The default CLI operation is directory listing. Download requires both
`--remote` and `--output`; upload/delete/rename are not implemented. See
`docs/MMS_FILE_TRANSFER.md` for API, safety, CLI, exit-code, and acceptance
details.

#### Public Alpha A1 model work

The live model now projects C#-style:

- `proposedLnTypeId` and `proposedDoTypeId`;
- `typeTemplates`;
- `variableTypeDiscoveries`;
- deterministic IED identity evidence;
- DataSet/RCB/control-block inventory.

Dynamic report configuration is separated from static model identity with three
fingerprints during migration:

- `fingerprint` — legacy canonical fingerprint retained for compatibility;
- `structuralFingerprint` — LD/LN/DO/DA + control identities, excluding mutable
  DataSet/RCB runtime state;
- `runtimeSnapshotFingerprint` — current DataSet inventory and RCB binding/state.

For IEDs with dynamic DataSets, a changed runtime fingerprint with an unchanged
structural fingerprint is expected and does not by itself mean the IED model or
firmware changed.

### Phase 3C — MMS service codecs

- MMS Initiate request/response and confirmed request/response/error envelopes.
- Invoke-ID routing, ObjectName, GetNameList, GetVariableAccessAttributes, Read, and Write.
- Recursive bounded TypeSpecification and mixed success/failure access results.
- C#-derived vectors, sanitizer, deterministic mutation smoke, and libFuzzer coverage.

### Phase 4A — offline DataSet and report monitoring

- DataSet and buffered/unbuffered RCB inventory from supplied GetNameList evidence.
- GetNamedVariableListAttributes request/response and IEC reference normalization.
- Strict InformationReport and OptFlds-driven report-frame decoding.
- RCB attribute read-plan/state mapping with cautious availability confidence.
- Sequence, configuration, DataSet, overflow, and segmentation continuity tracking.
- Bounded offline report monitor and dedicated MMS-reporting fuzz corpus.

### Hardware process-bus runtime — ESP32-P4 P0/P1/P2 foundation

The portable core still does not silently open raw sockets or physical IED sessions. **Active process-bus transmission exists only in explicitly enabled hardware/lab targets**, currently `embedded/esp32p4_smv_injector`, and is intended for isolated bench use.

The current ESP32-P4 target adds:

- RMII hardware bring-up;
- active Layer-2 SV TX;
- fixed/prebuilt runtime templates;
- deterministic GPTimer cadence;
- separate canonical vs diagnostic stream identities;
- 4I+4V balanced waveform generation;
- once-per-second realtime telemetry;
- a path toward EMAC hardware timestamps and PTP-disciplined synchronization.

See the embedded README/architecture files and `PROJECT_HANDOFF.md` for the exact current state.

## Phase 4B: association and report-subscription runtime

The C++ core now includes a transport-injected MMS association runtime and persistent RCB subscription state machine. It performs COTP, Session, Presentation, ACSE/MMS Initiate, confirmed-service routing, timeout/cancellation handling, RCB re-probe, URCB reservation, `RptEna`, optional GI, InformationReport delivery, and ownership-aware cleanup. See `ASSOCIATION_RUNTIME_PROFILE.md`.

The core deliberately does not ship a default socket transport in this phase. Applications that intentionally enable live IED access must provide an `MmsByteTransport` implementation.

## Build

### Windows

```powershell
cmake -S . -B build -A x64 -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Sanitizers

```bash
CC=clang CXX=clang++ cmake -S . -B build-sanitizers \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DARIEC61850_ENABLE_SANITIZERS=ON
cmake --build build-sanitizers --parallel
ctest --test-dir build-sanitizers --output-on-failure
```

## Read-only tools

### Inspect a COMTRADE record

```powershell
.\build\Release\ariec61850_comtrade_inspect.exe .\record.cfg --json
```

### Analyze a saved process-bus PCAP

```powershell
.\scripts\run-lab-check.ps1 -Pcap .\captures\device-session.pcap
```

## Live discovery quick start

Human summary:

```powershell
.\build\Release\ariec61850_live_discover.exe 192.168.1.10 102
```

Bounded read-only RCB evidence:

```powershell
.\build\Release\ariec61850_live_discover.exe `
  192.168.1.10 102 `
  --timeout-ms 30000 `
  --no-types `
  --no-datasets `
  --max-rcb 50 `
  --rcb-plan dynamic `
  --rcb-availability
```

Model JSON with bounded type discovery:

```powershell
New-Item -ItemType Directory -Force .artifacts\parity | Out-Null

.\build\Release\ariec61850_live_discover.exe `
  192.168.1.10 102 `
  --timeout-ms 120000 `
  --no-datasets `
  --no-rcb `
  --max-types 256 `
  --model-json `
  | Set-Content -Encoding utf8 .artifacts\parity\cpp-model.json
```

Inspect fingerprint/type evidence:

```powershell
$model = Get-Content .artifacts\parity\cpp-model.json -Raw | ConvertFrom-Json
$model.fingerprint
$model.structuralFingerprint
$model.runtimeSnapshotFingerprint
$model.typeTemplates.Count
$model.variableTypeDiscoveries.Count
```

## Same-IED C# ↔ C++ parity

The canonical comparator is:

```powershell
python .\tools\compare_live_model_json.py `
  .artifacts\parity\csharp\ied-model.json `
  .artifacts\parity\cpp-model.json
```

Optional evidence:

```powershell
python .\tools\compare_live_model_json.py `
  .artifacts\parity\csharp\ied-model.json `
  .artifacts\parity\cpp-model.json `
  --types `
  --runtime
```

Default pass/fail is structural. Runtime DataSet/RCB drift is informational.
The historical `scripts/compare-live-model-parity.py` path remains only as a
compatibility wrapper around this comparator.

See `docs/LIVE_MODEL_PARITY.md` for the complete C# export + comparison flow.

## Embedded direction

The wire core is being separated from host engineering services. Current target
roles are documented in `docs/EMBEDDED_TARGETS.md`:

- ESP32-P4-ETH: protocol/performance reference after Public Alpha;
- Waveshare ESP32-S3-POE-ETH-8DI-8DO: real I/O application reference;
- STM32H7/NXP class: later portability/industrialization target.

The embedded profile already excludes host TCP/live-discovery/PCAP/COMTRADE/SCL
parser tooling and provides a no-RTTI host-simulation gate. The portable MMS
file-service runtime is included through abstract transport/sink boundaries;
its filesystem-backed CLI remains host-only. Sampled Values also
has a caller-owned `encode_into(span)` path so the eventual publisher does not
need to allocate a fresh Ethernet frame buffer per sample.

## Validation boundary

Implemented code and green CI do not equal full field acceptance. Current
validation includes GCC/Clang/MSVC builds, deterministic tests, sanitizer/fuzz
workflows, and selected live read-only interoperability evidence. Full
replacement claims still require same-IED C#↔C++ evidence, repeated live cycles,
packet capture review, and multi-vendor physical/simulator interoperability.

See `docs/FEATURE_MATRIX.md` for a conservative status table and
`PHASE_4C1_LIVE_MODEL_INTEROP.md` for the live-evidence boundary.

See `REPORTING_PROFILE.md` for the offline report decoder profile, `LAB_INTEROP_CHECKLIST.md` before using a physical IED, and `MIGRATION_CHECKLIST.md` for the detailed progress ledger.

## North-star reminder

ARStack is not intended to become a UI-heavy packet viewer with timing bolted on later. The architecture priority is:

```text
standards truth
    > measured hardware evidence
    > deterministic execution
    > interoperability evidence
    > usability
    > convenience hacks
```

The long-term product family — injector, process-bus analyzer, digital-substation probe and real-time MU/closed-loop simulator — should share the same real-time/evidence core.

## Public-repository naming policy

Public project documentation should remain vendor-neutral. Do not add commercial competitor names, product comparisons, logos, screenshots, or vendor marketing references. Standards, protocol identifiers, and technical platform/dependency names required to build or reproduce the project are allowed.
