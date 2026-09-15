# ARStack Studio P0 public-release contract

**Milestone status: RELEASED · stable public `v0.1.0` · 2026-09-15**

Public release: https://github.com/masarray/arstack61850/releases/tag/v0.1.0

Production source target: `9c7fc7300220db4643e5643081240b955cfe12df`
Accepted binary build head: `d9b5b6848415c7e6d1c52ec929e57c66b608058d`
Public release workflow source: ARStack Studio Release run `34918302946` (SUCCESS)

## Product target

The first public ARStack Studio release is intentionally bounded to one professional workflow:

> **IEC 61850 Sampled Values Injector / Generator — 4I + 4V**

The milestone packages the ESP32-P4 publisher as a usable Windows instrument without requiring the operator to install or understand the development toolchain.

## P0 profile boundary

- 4 current channels: `Ia`, `Ib`, `Ic`, `In`
- 4 voltage channels: `Ua`, `Ub`, `Uc`, `Un`
- one `INT32 value + Quality` pair per channel
- 16 ordered FCDA leaves
- 64-byte sample payload
- one ASDU
- 4000 samples/s reference profile
- sample-counter cycle `0..3999`
- SCL/CID/SCD/IID import remains available when the compiled stream fits the same firmware boundary
- `smpSynch=0` until separately measured disciplined-clock evidence exists

Profiles outside this layout may still be parsed and explained, but they are not v0.1.0 deployment targets and must remain blocked from deployment.

## Closed work packages

### P0.1 — Canonical Quick Start — CLOSED

- [x] ARStack-owned 4I+4V reference SCL bundled with the native Qt application
- [x] canonical C++ SCL parser/profile compiler path
- [x] Class-A reference profile at 4000 fps / modulus 4000 / 64-byte payload / 16 leaves
- [x] one-click `4I+4V Quick Start` operator path

### P0.2 — Release UI / UX — CLOSED

- [x] ARStack Studio is the canonical public operator surface
- [x] device, firmware, profile and output states are explicit
- [x] Current / Voltage matrices remain the primary work surface
- [x] disabled Start/Deploy paths expose operator-readable reasons
- [x] Expert diagnostics remain secondary to the normal workflow

### P0.3 — ESP32-P4 firmware/session manager — CLOSED FOR v0.1.0

- [x] typed semantic device identity and protocol versioning
- [x] packaged firmware image + manifest + SHA-256 enforcement
- [x] bounded serial ownership and firmware handoff
- [x] reconnect verification after firmware operations
- [x] wrong-device and incompatible-image paths fail closed
- [x] single-instance/session ownership hardening

### P0.4 — Windows public distribution — CLOSED

- [x] native Release build
- [x] bundled Qt runtime
- [x] portable package
- [x] Windows installer
- [x] clean Windows package smoke
- [x] versioned firmware bundle and manifest
- [x] SHA-256 checksums and release provenance
- [x] stable tagged GitHub Release `v0.1.0`

### P0.5 — Physical product acceptance — ACCEPTED

Executed evidence used for the public milestone:

- [x] 10/10 Start/Stop cycles
- [x] Quick Start profile deploy/start path on the accepted physical board
- [x] live magnitude / phase / frequency / Quality / CT edits
- [x] Zero operation
- [x] uninterrupted 3600-second retained run at 4000 fps
- [x] `missed=0`, `txFailures=0`, `healthReconnects=0`, `automaticStarts=0`
- [x] independent capture spot-check of destination MAC, APPID, VLAN/PCP, svID, confRev, one ASDU, 64-byte payload, `smpCnt` continuity and 3999 -> 0 wrap
- [x] `smpSynch=0` truth boundary retained

The following strict-lab cases were intentionally deferred as non-blocking follow-on validation and are **not represented as completed physical PASS cases**:

- [ ] exhaustive blank-board/recovery permutations across USB-driver combinations
- [ ] exhaustive external engineering SCL physical matrix beyond the accepted P0 reference path
- [ ] broader power-cycle/unplug/replug permutations beyond the product-owner pragmatic release gate

## Published assets

```text
877a35dbbe110cc8ab740d3fcdc44a63f348405dc509da3164d9aaa92e9e2622  ARStack-Studio-0.1.0-win-x64-setup.exe
1487e90254ddc5cab0f7b917580be951311c1697bec6dc19d2e2720683f00f83  ARStack-Studio-0.1.0-win-x64-portable.zip
fae413d7b6f65b7d10ca65dc277fbf586d487f21209a29875036791088afa9c7  arstack-esp32p4-smv-0.1.0.bin
```

## Explicitly outside v0.1.0

- generalized 3I / 8I / 8I+4V / 9I+6V / 12I+4V / 12I+8V runtime layouts
- multi-stream publication
- arbitrary DataSet layouts
- COMTRADE playback as a released device data plane
- scripted fault sequences
- full Process Bus analyzer
- protection-grade timing claims
- IEC 61850 conformance/certification claims

The P0 boundary can widen only through a later versioned milestone. Public v0.1.0 remains a deliberately bounded engineering instrument release.
