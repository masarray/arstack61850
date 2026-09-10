# ARStack Studio — 4I + 4V SMV Injector / Generator

ARStack Studio is the **canonical native desktop operator surface** for the first ARStack61850 public Sampled Values release. It is a Qt 6 / C++ / QML application; the ESP32-P4 remains the deterministic real-time publisher.

## P0 public boundary

The first release is intentionally narrow and testable:

- IEC 61850 Sampled Values, 9-2LE-style **4 current + 4 voltage**;
- wire order `Ia, Ib, Ic, In, Ua, Ub, Uc, Un`;
- one INT32 value plus one 32-bit Quality word for each signal;
- 16 FCDA leaves / 64-byte sample payload;
- **4000 frames/s**, `SmpPerSec`;
- one ASDU per frame;
- explicit `smpCnt` modulus 4000;
- ESP32-P4-ETH target running the ARStack protocol-v1 firmware.

Broader codecs, SCL parsing, COMTRADE support, experimental timing work, or other layouts elsewhere in the repository do **not** expand this P0 deployment boundary.

## Architecture

```text
4I+4V Quick Start or compatible SCL/CID/SCD/IID
  -> ARIEC61850 SclParser
  -> SvPublisherProfileCompiler
  -> fail-closed ESP32-P4 deployment classifier
  -> Qt SclProfileModel
  -> operator Deploy
  -> guarded Qt DeviceController
  -> ARStack protocol v1
  -> deterministic ESP32-P4 SMV publisher
```

The desktop application never becomes the 4000 fps timing source. It configures and supervises the embedded publisher.

## Normal operator workflow

1. Open **ARStack Studio**.
2. Choose **4I+4V Quick Start**, or **Open SCL** for a compatible external profile.
3. Connect the ESP32-P4-ETH board. Studio verifies the `ARSTACK identity` handshake and protocol.
4. **Deploy** the validated Class-A 4I+4V profile.
5. Adjust Current / Voltage setpoints as required.
6. **Start** Sampled Values output.
7. **Stop** before changing immutable profile identity fields or before leaving the test setup.

The Home ribbon exposes explicit `DEVICE`, `FW`, `PROFILE`, and `OUTPUT` state. Disabled Deploy/Start controls explain the blocking condition instead of silently doing nothing.

Critical START and DEPLOY rules are repeated in the native controller, so keyboard shortcuts or alternate QML paths cannot bypass the public P0 safety boundary.

## Blank board / firmware recovery

For a blank board, recovery board, or protocol mismatch, open **Configuration → Firmware**.

The Firmware Manager uses this fail-closed sequence:

1. select the USB serial port;
2. **Verify ESP32-P4** using the ROM flasher connection;
3. validate `firmware-manifest.json`;
4. verify the packaged firmware SHA-256;
5. unlock **Install / Recover** only for a verified ESP32-P4;
6. write the single merged recovery image at flash offset `0x0`;
7. reset the chip;
8. reconnect and require the normal ARStack `IDENTIFY` handshake.

A missing flasher, missing image, malformed manifest, incompatible chip declaration, unsafe flash offset, protocol mismatch, or SHA-256 mismatch keeps installation blocked.

The redistributable Windows package includes a pinned standalone `espflash`; the operator does **not** need Rust, Python, ESP-IDF, CMake, Qt, or a source checkout.

## Firmware package contract

A release package contains:

```text
ARStackStudio.exe
firmware/
  firmware-manifest.json
  arstack-esp32p4-smv-0.1.0.bin
  SHA256SUMS.txt
tools/
  espflash.exe
licenses/
  espflash/
```

Manifest schema `arstack.studio.firmware.v1` binds the image to chip `esp32p4`, firmware version `0.1.0`, protocol `1`, flash offset `0`, the exact image filename, image SHA-256, ESP-IDF build version, and source commit.

Protocol `1` is the P0 GUI/firmware capability contract for the supported `SMV-4I4V`, profile deployment, and live setpoint workflow.

## Windows release artifacts

The P0 release workflow creates separate downloadable files:

- `ARStack-Studio-0.1.0-win-x64-portable.zip`
- `ARStack-Studio-0.1.0-win-x64-setup.exe`
- `SHA256SUMS.txt`

Both packages contain the Qt runtime, verified ESP32-P4 firmware bundle, and pinned standalone flasher. The installer is per-user and does not require administrator elevation.

A `v0.1.0` tag is the only accepted P0 publish tag. The GitHub release is intentionally marked prerelease until physical P0 acceptance is complete.

## Truthfulness boundary

Phasor and waveform views are generated setpoint previews, not independent Ethernet capture evidence. `smpSynch` remains an embedded synchronization truth and must not be promoted to a synchronization claim without measured evidence.

CI can prove parser/compiler behavior, firmware manifest/hash enforcement, native START/DEPLOY guards, Qt instantiation, high-DPI launch, firmware build/package generation, Windows portable construction, installer construction, and packaged-app smoke execution. CI cannot prove physical Ethernet waveform or timing behavior.

The final physical gate is maintained in [`../../docs/SMV_STUDIO_P0_RELEASE.md`](../../docs/SMV_STUDIO_P0_RELEASE.md).

## Development build

Qt 6.5+ is required with Core, Gui, Qml, Quick, QuickControls2 and SerialPort, together with a C++20 compiler and CMake 3.24+.

From a Qt-enabled Windows developer shell:

```powershell
cmake -S apps/arstack_studio -B build-arstack-studio -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-arstack-studio --target arstack_studio --parallel
.\build-arstack-studio\arstack_studio.exe
```

The repository also provides:

```powershell
.\apps\arstack_studio\run.cmd
```

or:

```powershell
.\apps\arstack_studio\run-windows.ps1
```

These developer launchers are not required by the redistributable release.

## Automated Studio gates

`.github/workflows/arstack-studio-qt.yml` checks:

- native Qt configure/build with warnings as errors;
- bundled 4I+4V reference profile contract;
- native P0 controller fail-closed policy;
- valid firmware bundle acceptance;
- corrupted firmware SHA-256 rejection;
- normal offscreen QML launch;
- high-DPI QML launch;
- Windows launcher PowerShell parsing.

`.github/workflows/arstack-studio-release.yml` additionally builds the merged ESP32-P4 firmware package and the Windows portable/installer release candidates.
