# ARStack developer documentation

This index is for engineers who want to study or extend ARStack's source code. If you are new to ARStack, start with the [user-facing quick start](GETTING_STARTED.md).

## Architecture and project direction

- [Project north star](../NORTH_STAR.md) — overall protocol, execution and hardware design.
- [Project handoff](../PROJECT_HANDOFF.md) — current integration context for maintainers.
- [Studio architecture and operator workflow](../apps/arstack_studio/README.md).
- [ESP32-P4 firmware architecture](../embedded/esp32p4_smv_injector/ARCHITECTURE.md).

## Protocol and engineering tools

- [Feature matrix](FEATURE_MATRIX.md) — implemented components and evidence categories.
- [SCL injection workflow](LIVE_INJECTION_WORKFLOW.md).
- [MMS file transfer](MMS_FILE_TRANSFER.md).
- [SCL-assisted MMS connection](SCL_ASSISTED_MMS_CONNECT_PROFILE.md).
- [PTP timing and lab implementation](PTP_LAB_TIMING.md).

## Release and test evidence

- [M3.H release record](releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md).
- [Mainline release/source reconciliation](integration/M3H_MAINLINE_RELEASE_SOURCE_CLOSURE_2026-10-24.md).
- [Historical milestone notes](SMV_STUDIO_V0.1.0_PUBLIC_RELEASE.md).

## Building

Portable C++20 core:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Desktop Studio requirements and developer commands are in the [Studio README](../apps/arstack_studio/README.md). Product installation does not require compiling from source.
