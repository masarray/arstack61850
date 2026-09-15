# ARStack Studio / SMV Injector v0.1.0

**Stable public release · 15 September 2026**

[Download v0.1.0](https://github.com/masarray/arstack61850/releases/tag/v0.1.0)

## Milestone

`v0.1.0` closes the first public ARStack Sampled Values product milestone: a Windows engineering application supervising a deterministic ESP32-P4 publisher for the validated 4I+4V / 4000 fps profile.

This milestone is deliberately narrower than the full ARStack roadmap. It is a usable public engineering release, not a protection-grade timing or conformance-certification claim.

## Public package

- Windows x64 installer — recommended for normal use
- Windows x64 portable package
- ESP32-P4 firmware image
- firmware manifest
- release SHA-256 ledger
- bundled firmware recovery/flash tooling inside ARStack Studio

## Validated profile

| Property | v0.1.0 value |
|---|---|
| Channels | 4 current + 4 voltage |
| Frame rate | 4000 frames/s |
| ASDU | 1 per frame |
| Sample payload | 64 bytes |
| `smpCnt` modulus | 4000 |
| `smpSynch` | 0 |
| Firmware protocol | ARStack protocol v1 |

## Physical acceptance summary

The accepted physical board run completed:

- 10/10 Start/Stop cycles;
- live magnitude, phase, frequency, Quality and CT edits;
- Zero;
- uninterrupted 3600-second retained run;
- 4000 fps minimum and maximum throughout retained telemetry;
- zero missed sample slots;
- zero TX failures;
- zero health reconnects;
- zero automatic Start events.

An independent capture spot-check contained 39,524 primary SV frames over approximately 9.881 seconds, an effective rate of approximately 3999.899 fps, zero observed `smpCnt` discontinuities, and ten 3999 -> 0 wraps. The capture matched the accepted P0 wire identity and retained `smpSynch=0`.

## Release provenance

| Item | Value |
|---|---|
| Stable tag | `v0.1.0` |
| Production merge | `9c7fc7300220db4643e5643081240b955cfe12df` |
| Accepted binary build head | `d9b5b6848415c7e6d1c52ec929e57c66b608058d` |
| Release build | ARStack Studio Release run `34918302946` — SUCCESS |
| Public publish workflow | Publish Public SMV Injector v0.1.0 run `34933705055` — SUCCESS |

The `v0.1.0` tag points directly to the production merge SHA above.

## Published SHA-256

```text
877a35dbbe110cc8ab740d3fcdc44a63f348405dc509da3164d9aaa92e9e2622  ARStack-Studio-0.1.0-win-x64-setup.exe
1487e90254ddc5cab0f7b917580be951311c1697bec6dc19d2e2720683f00f83  ARStack-Studio-0.1.0-win-x64-portable.zip
fae413d7b6f65b7d10ca65dc277fbf586d487f21209a29875036791088afa9c7  arstack-esp32p4-smv-0.1.0.bin
```

## Product boundary after this milestone

The next SMV work may expand timing evidence, profiles, multi-stream support, replay/sequencing and interoperability. None of that changes the truth of this release:

> v0.1.0 is the stable public 4I+4V / 4000 fps engineering-instrument baseline.

IED Simulator / IEC 61850 Workbench development is a separate product track and should not be used to redefine the v0.1.0 SMV acceptance boundary.
