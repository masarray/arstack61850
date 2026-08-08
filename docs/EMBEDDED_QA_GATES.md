# Embedded QA gates

arstack61850 deliberately separates software evidence from physical-hardware evidence. A green CI run must never be described as proof of protection-grade timing or successful Ethernet transmission on silicon.

## Gate A — portable embedded host simulation

Runs on Linux with both GCC and Clang, using the MCU-oriented source boundary and host-service exclusions.

Required evidence:

- embedded source-boundary audit passes;
- strict warnings-as-errors build passes;
- full embedded link smoke passes;
- SV publisher edge-case smoke passes;
- deterministic 8,000-frame simulation passes at a virtual 4,000 samples/s;
- C++ publisher/encoder emits a two-second classic-PCAP capture;
- an independent pure-Python reference implementation rebuilds the expected Ethernet + process-bus + BER SV stream and requires the entire PCAP to match byte-for-byte;
- the independent parser also validates destination/source MAC, EtherType `0x88BA`, APPID `0x4001`, declared length, reserved fields, `noASDU`, `svID`, DataSet reference, `confRev`, `smpCnt`, `smpSynch`, `smpRate`, `smpMod`, 64-byte synthetic payload, and exact 250 us capture cadence;
- zero simulated lateness is reported when the supplied monotonic clock is exact;
- JUnit, PCAP, and machine-readable JSON QA evidence are retained as GitHub Actions artifacts for both GCC and Clang.

The external verifier intentionally does not import or call the arstack61850 decoder. This prevents a matching encoder/decoder defect from satisfying Gate A merely because both implementations accept the same incorrect wire image.

This gate proves deterministic software behavior and exact first-trial wire construction under a virtual clock. It does not measure wall-clock performance.

## Gate B — real ESP32-P4 cross-compile

Uses the official ESP-IDF v6.0.2 toolchain with target `esp32p4`.

Required evidence:

- the narrow arstack61850 ESP-IDF component compiles and links;
- the compile-only integration smoke app builds;
- the flashable first-trial SV firmware builds and links;
- the resulting ESP-IDF flasher manifest identifies chip `esp32p4`;
- no host-only TCP/filesystem/capture services leak into the embedded component boundary.

This gate proves that the code is accepted by the real ESP32-P4 RISC-V compiler/linker and ESP-IDF component system.

## Gate C — flashable artifact completeness

The ESP-IDF workflow must fail if any required deployment output is missing, empty, malformed, targets another chip, or disagrees with the expected first-trial flash layout.

The authoritative deployment source for this gate is ESP-IDF's generated `flasher_args.json`. Offset strings are normalized to numeric addresses before comparison so equivalent formatting cannot create a false failure. The gate requires:

- `extra_esptool_args.chip == esp32p4`;
- bootloader at `0x2000`;
- partition table at `0x8000`;
- application at `0x10000`;
- every file referenced by that three-image manifest exists and is non-empty;
- the generated `flash_args` companion exists and is non-empty;
- the repository-controlled `sdkconfig.defaults` baseline exists and is non-empty.

`flash_args` is retained for flashing convenience but is not separately reparsed as a second authority; duplicating ESP-IDF's own manifest interpretation would make the CI brittle without adding independent deployment evidence.

The retained `esp32p4-sv-trial-firmware-*` artifact contains:

- application `.bin`;
- application `.elf`;
- application `.map`;
- bootloader image;
- partition-table image;
- `flash_args`;
- validated `flasher_args.json`;
- `sdkconfig.defaults` used as the repository-controlled configuration baseline;
- generated `ARSTACK_BUILD_PROFILE.json` recording `FLASHABLE/READY`, target chip, image sizes, and flash layout;
- generated `ARSTACK_BUILD_FILE_LIST.txt` inventory for reproducibility/debugging;
- `SHA256SUMS` covering the deployment-critical images, manifests, inventory, and configuration baseline;
- firmware-specific flashing/behavior README.

If Gate C fails, CI uploads a short-lived `esp32p4-gate-c-diagnostics-*` artifact containing the actual generated manifest, flash arguments, inventory, and core images. This keeps failures inspectable without weakening the success criteria.

Passing Gate C means a developer can download a CI-produced firmware set instead of rebuilding it locally, verify the retained files, and use the ESP-IDF-generated flash layout for the target board.

## Gate D — physical ESP32-P4 Ethernet acceptance

This gate requires the actual board and an isolated laboratory Ethernet segment.

Required evidence for the first Sampled Values proof:

1. ESP32-P4 boots the Gate-C firmware and the PHY reports link up.
2. Wireshark observes EtherType `0x88BA` frames from the board.
3. Destination MAC and APPID are exactly the configured first-trial values.
4. Observed packet rate is near 4,000 frames/s under the 50 Hz / 80 samples-per-cycle profile.
5. `smpCnt` behavior matches the accepted publisher semantics and wraps at 4,000.
6. No unexpected catch-up burst appears after scheduler delay.
7. TX/encode failures, timer coalescing, lateness, and heap-watermark telemetry are recorded.
8. Short run, 10-minute soak, and one-hour soak are progressively accepted.

Only Gate D can validate real PHY wiring, EMAC/DMA behavior, scheduler jitter on silicon, cable transmission, switch behavior, and packet continuity observed by another device.

## Status vocabulary

Use these terms in issues, PRs, release notes, and test evidence:

- `SIMULATED/PASSED` — Gate A passed, including the independent exact-byte PCAP oracle.
- `ESP32P4/CROSS-COMPILED` — Gate B passed.
- `FLASHABLE/READY` — Gate C passed.
- `HARDWARE/PASSED` — Gate D passed on a recorded physical run.

Never promote `FLASHABLE/READY` to `HARDWARE/PASSED` without physical evidence.
