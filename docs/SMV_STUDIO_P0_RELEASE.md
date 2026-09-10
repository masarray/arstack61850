# ARStack Studio P0 public-release contract

## Product target

The first public ARStack Studio release is intentionally bounded to one professional workflow:

> **IEC 61850 Sampled Values Injector / Generator — 4I + 4V**

The release must make the already proven ESP32-P4 publisher usable as a clean desktop instrument without requiring the operator to install or understand the development toolchain.

## P0 profile boundary

- 4 current channels: `Ia`, `Ib`, `Ic`, `In`
- 4 voltage channels: `Ua`, `Ub`, `Uc`, `Un`
- one `INT32 value + Quality` pair per channel
- 16 ordered FCDA leaves
- 64-byte sample payload
- one ASDU
- 4000 samples/s reference profile
- sample-counter cycle `0..3999` for the bundled ARStack reference profile
- SCL/CID/SCD/IID import remains available when the compiled stream fits the same firmware boundary

Profiles outside this layout may still be parsed and explained, but they are not P0 release targets and must remain blocked from deployment.

## P0 work packages

### P0.1 — Canonical Quick Start

- [x] ARStack-owned 4I+4V reference SCL bundled with the native Qt application
- [x] template goes through the canonical C++ SCL parser/profile compiler
- [x] application-owned reference counter policy resolves the template to Class A
- [x] Home ribbon exposes one-click `4I+4V Quick Start`
- [x] CI regression checks Class A, device support, 4000 fps, 4000 modulus, 64-byte payload and 16 leaves
- [ ] first-run visual state makes `Quick Start -> Connect -> Deploy -> Start` self-explanatory

### P0.2 — Release UI / UX polish

- [ ] make ARStack Studio the only canonical public operator surface
- [ ] present device, firmware, profile and output state in one compact hierarchy
- [ ] keep Current / Voltage matrices as the primary work surface
- [ ] remove development-language friction from normal workflow
- [ ] ensure every disabled Deploy/Start state has an operator-readable reason
- [ ] preserve Expert diagnostics without making them primary UI
- [ ] verify keyboard, resizing, DPI scaling and laptop layouts

### P0.3 — ESP32-P4-ETH Firmware Manager

- [ ] identify supported ESP32-P4-ETH target before flash
- [ ] version GUI <-> firmware protocol/capabilities
- [ ] package a release firmware image and manifest with hashes
- [ ] install firmware from inside ARStack Studio without ESP-IDF
- [ ] support firmware update and recovery flow
- [ ] reset/reconnect and verify `IDENTIFY` after flash
- [ ] refuse incompatible target/image combinations

### P0.4 — Windows public distribution

- [ ] native Release build
- [ ] bundle required Qt runtime with deployment tooling
- [ ] portable package
- [ ] Windows installer
- [ ] clean-machine smoke test with no Qt/Python/CMake/Ninja/ESP-IDF installed
- [ ] versioned firmware bundled or release-resolved by manifest
- [ ] SHA-256 checksums and reproducible release metadata
- [ ] tagged GitHub Release workflow

### P0.5 — Physical acceptance before public release

- [ ] blank/recovery ESP32-P4-ETH -> Firmware Manager -> verified device
- [ ] bundled 4I+4V Quick Start -> Deploy -> Start
- [ ] external 4I+4V engineering SCL -> compile -> Deploy -> Start
- [ ] 4000 fps retained run with no unexpected missed slots / canonical TX failures
- [ ] live I/U/phase/frequency/quality edits without unintended `smpCnt` restart
- [ ] Stop / Zero / reconnect / power-cycle behavior
- [ ] trusted independent capture of destination MAC, APPID, VLAN/PCP, svID, confRev and counter continuity
- [ ] no synchronization claim beyond measured evidence; `smpSynch` truth boundary retained

## Explicitly outside the first release

The following are valuable follow-on work but must not delay or destabilize P0:

- 3I / 8I / 8I+4V / 9I+6V / 12I+4V / 12I+8V runtime layouts
- multi-stream publication
- generalized arbitrary DataSet layouts
- COMTRADE playback
- scripted fault sequences
- full Process Bus analyzer
- formal conformance/certification claims

This boundary can be widened only after the 4I+4V product path is packaged, recoverable, independently verified and usable by a new operator without development tooling.
