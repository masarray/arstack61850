# ARStack Studio M3.H.1 — Cross-Platform Packaging Update

**Windows and Linux desktop packages · same verified M3.H SV/PTP core**

This packaging update adds a Linux x86_64 AppImage to the existing Windows installer and portable packages. **No ESP32-P4 firmware or SV/PTP timing implementation has changed.** The Linux AppImage is built from the exact existing M3.H tag and validated with native/headless AppImage checks. Existing Windows binaries and firmware are copied byte-for-byte from the original stable release, with matching SHA-256.

## Download

Choose the file for your operating system:

- **Windows 10/11 x64:** `ARStack-ProcessBus-M3H-win-x64-setup.exe` or Portable ZIP.
- **Linux x86_64:** `ARStack-Studio-linux-x86_64.AppImage`, with matching `.sha256` checksum.
- **ESP32-P4 board:** the existing matched firmware image, bundled with the Windows packages and included in the Linux AppImage.

Use [Getting Started](https://github.com/masarray/arstack61850/blob/main/docs/GETTING_STARTED.md) to begin. The same 4I+4V / 4,000 frames/s SV publisher and laboratory PTP grandmaster emulator are included.

## Engineering evidence

The M3.H acceptance capture measured 163,048 contiguous Sampled Values packets, 3,999.884 frames/s and `smpSynch=2` throughout the recorded interval. The [original technical record](https://github.com/masarray/arstack61850/blob/main/docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md) contains measured details.

The Windows Setup/Portable and firmware assets retain their original hashes and source build identity; this release changes only cross-platform distribution. Run active SV/PTP experiments on an **isolated laboratory Ethernet network**.
