# IEDScout Product Release Evidence

This document is the release-candidate evidence ledger for the ARStack IEC 61850 desktop workbench. It deliberately distinguishes what is proven live, what is proven against the deterministic simulator/runtime harnesses, what is offline/model tested, and what is not claimed.

## Evidence classes

- **LIVE-PROVEN** — exercised through a real socket, association, raw Ethernet transport, deployed executable, or installed application path rather than only a model/unit helper.
- **SIMULATOR-PROVEN** — exercised end-to-end against ARStack's deterministic MMS/IED simulator runtime, including real protocol encoding/transport where the harness provides it.
- **OFFLINE-TESTED** — deterministic parser/model/serialization or file-level behavior that does not require a live peer.
- **NOT CLAIMED** — intentionally unsupported, unavailable in the automated environment, or not safe to infer from another evidence class.

A feature may have more than one evidence class. The strongest applicable class is stated without upgrading adjacent behavior that was not actually exercised.

## Release-candidate matrix

| Product surface | Evidence class | Proven release behavior | Important boundary |
| --- | --- | --- | --- |
| IED Connection | LIVE-PROVEN + SIMULATOR-PROVEN | persistent MMS association; live discovery; exact-type Read; guarded SP/CF/DC/SE scalar Write; verification Read; stale-generation rejection; 24-cycle reconnect/clean-disconnect soak | no automatic Write retry; startup restores endpoint but deliberately does not auto-connect |
| Reports | SIMULATOR-PROVEN | DataSet/URCB/BRCB inventory; enable/GI/disable-release; retained cleanup/reacquire; decoded report stream; EntryID/overflow indicators | replay/rewind mutation is not claimed where the reusable client runtime does not expose it |
| Files | SIMULATOR-PROVEN | bounded FileDirectory pagination and streaming download with FileClose/partial-output cleanup | remote upload/delete/rename are NOT CLAIMED |
| Settings | SIMULATOR-PROVEN | SGCB inventory/deep read; guarded ActSG activation followed by verification Read | full EditSG -> SE edits -> CnfEdit transaction is NOT CLAIMED |
| SCL | OFFLINE-TESTED + LIVE-UI-PROVEN | exact-source Save As; deterministic canonical reconstruction; Ed1/Ed2/Ed2.1 normalized conversion; guarded SCD/ICD/CID; semantic round trip | canonical conversion does not claim lossless unknown vendor XML/extensions; exact-source Save As is the vendor-lossless path |
| GOOSE Monitor/Publisher | LIVE-PROVEN + SIMULATOR-PROVEN | explicit NIC binding; real Layer-2 publisher path; APPID/MAC/VLAN/goID/DataSet/ConfRev; stNum/sqNum/TTL; bounded monitor tables; PCAP export | Windows raw Ethernet requires Npcap runtime availability |
| Simulator | LIVE-PROVEN + SIMULATOR-PROVEN | child MMS server lifecycle; multi-IED same-port/distinct-address coexistence; live value plane; direct/SBO normal and enhanced controls; URCB/BRCB; bounded start/stop/restart | broader Sampled Values/PTP/embedded platform roadmap is not a desktop RC blocker |
| Product persistence | OFFLINE-TESTED + UI-PROVEN | atomic versioned state; workspace restore; eight-entry deduplicated recent endpoint list; corrupt/unknown state fail-closed recovery | automatic connection on startup is intentionally disabled |
| Windows package | DEPLOYED/INSTALLED-PROVEN when release workflow is green | `windeployqt` staging; portable ZIP; NSIS installer; silent install; staged and installed smoke test; helper server and Qt runtime verification | installer does not silently redistribute Npcap; runtime guidance remains explicit |

## Runtime and shutdown release gates

The release-candidate branch must pass all of the following on the same executable behavior head:

- `PRODUCT_HARDENING_PASS`
- `PRODUCT_HARDENING_NEGATIVE_PASS`
- `WINDOWS_RUNTIME_READINESS_PASS` on the Windows release job
- `MMS_CLIENT_RECONNECT_SOAK_PASS cycles=24`
- `APPLICATION_CLOSE_SOAK_PASS cycles=12 ... orphan_children=0 socket_release=pass`
- `WINDOWS_RELEASE_PACKAGE_PASS ... staged_smoke=pass installed_smoke=pass qt_runtime=pass helper_server=pass npcap_guidance=pass installer=nsis portable=zip`
- retained `RUNTIME_LIFECYCLE_PASS`, large-SCL performance, responsiveness, GOOSE, live-data, control, URCB/BRCB and multi-IED gates from `IED Simulator Qt`.

The application-close soak is intentionally process-external. Each cycle starts the real Qt workbench and its MMS helper, proves the configured endpoint becomes live, allows normal application exit, then rejects any orphan helper process or unreleased listen socket.

## Windows / Npcap policy

The Windows application checks for both `wpcap` and `Packet` runtime libraries. If they are unavailable, the application remains usable for MMS/SCL/simulator workflows and explicitly reports that Npcap must be installed before Windows raw-Ethernet GOOSE Monitor/Publisher use. The release package does not disguise a missing Npcap runtime as a working raw-Ethernet path.

## External-vendor interoperability status

No third-party vendor IED or proprietary vendor simulator is attached to the automated GitHub runner used for this release track. Therefore **multi-vendor field interoperability is NOT CLAIMED as automated release evidence**. The RC evidence instead records real MMS wire behavior, deterministic ARStack server/client interoperability, real Layer-2 GOOSE encoding/publication evidence, control/report/file/settings protocol harnesses, and explicit vendor-neutral fail-closed boundaries.

When external vendor hardware/simulators are available, results should be appended as vendor-specific evidence rather than retroactively relabeling simulator proof as vendor proof. Lack of attached vendor hardware is not used to fabricate a pass or fail.

## Known intentional limitations at RC

- MMS remote file mutation (upload/delete/rename): **NOT CLAIMED**.
- Full Setting Group edit transaction: **NOT CLAIMED**.
- Lossless unknown vendor XML/extensions during canonical SCL conversion: **NOT CLAIMED**; exact-source Save As preserves the source bytes.
- Automatic field-device connection after application restart: intentionally disabled.
- Windows raw Ethernet without Npcap: unavailable by design and surfaced with operator guidance.
- Sampled Values runtime simulation, PTP, ESP/embedded and broader process-bus platform work remain separate ARStack platform tracks and are not desktop RC prerequisites.

## Packaging artifacts

The dedicated `IED Simulator Release Hardening` workflow produces two Windows RC artifacts from the same staged directory:

1. `ARStack-IEC61850-Workbench-Setup-win64.exe` — installable NSIS package.
2. `ARStack-IEC61850-Workbench-portable-win64.zip` — portable deployed tree.

Both contain the workbench executable, the simulator helper executable and the Qt runtime/QML/plugin deployment produced by `windeployqt`. The workflow smoke-tests the staged application and a silent-installed copy before uploading either artifact.
