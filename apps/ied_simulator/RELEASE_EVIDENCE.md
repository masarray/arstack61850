# external IEC 61850 client Product Release Evidence

## v0.2.0 stable-release candidate

Current promotion branch includes the accepted external IEC 61850 client parity baseline through P1 reporting golden parity. External external vendor external IEC 61850 client retest on 2026-09-19 using `Siprotec_084F06BCU_AA1E1F06R4.cid` demonstrated full discovery of 32 logical devices / 4925 typed points, URCB and BRCB report delivery, SBO Enhanced Open/Close with external `ctlNum=0`, positive CommandTermination before process feedback, and BRCB SqNum progression 1 -> 2 -> 3. The same diagnostic also retained successful control with ctlNum 1/2 on a second client path.

This evidence is specific to the tested external vendor external IEC 61850 client + reference IEC 61850 model CID profile. It is not an IEC 61850 conformance certificate and is not generalized into universal multi-vendor interoperability.

P1 regression head `68cad00fed15bf77104593c93bfd66d5c5590d1c` passed all eight triggered workflows, including external IEC 61850 client Parity 11/11 on Linux GCC, Linux Clang and Windows/MSVC. P1 was merged into the accepted baseline as `17a6932633328daef8647fc6e4b18a053df6b4e1` before stable-release promotion.

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
| Reports | LIVE-PROVEN + SIMULATOR-PROVEN | DataSet/URCB/BRCB inventory; enable/GI/disable-release; decoded report stream; EntryID/ConfRev/SqNum/ReasonForInclusion; external vendor external IEC 61850 client URCB/BRCB acceptance on the tested reference IEC 61850 model profile | replay/rewind mutation is not claimed where the reusable client runtime does not expose it |
| Files | SIMULATOR-PROVEN | bounded FileDirectory pagination and streaming download with FileClose/partial-output cleanup | remote upload/delete/rename are NOT CLAIMED |
| Settings | SIMULATOR-PROVEN | SGCB inventory/deep read; guarded ActSG activation followed by verification Read | full EditSG -> SE edits -> CnfEdit transaction is NOT CLAIMED |
| SCL | OFFLINE-TESTED + LIVE-UI-PROVEN | exact-source Save As; deterministic canonical reconstruction; Ed1/Ed2/Ed2.1 normalized conversion; guarded SCD/ICD/CID; semantic round trip | canonical conversion does not claim lossless unknown vendor XML/extensions; exact-source Save As is the vendor-lossless path |
| GOOSE Monitor/Publisher | LIVE-PROVEN + SIMULATOR-PROVEN | explicit NIC binding; real Layer-2 publisher path; APPID/MAC/VLAN/goID/DataSet/ConfRev; stNum/sqNum/TTL; bounded monitor tables; PCAP export | Windows raw Ethernet requires Npcap runtime availability |
| Simulator | LIVE-PROVEN + SIMULATOR-PROVEN | child MMS server lifecycle; multi-IED same-port/distinct-address coexistence; live value plane; direct/SBO normal and enhanced controls; URCB/BRCB; external vendor external IEC 61850 client discovery/report/control acceptance for the tested reference IEC 61850 model CID; bounded start/stop/restart | broader Sampled Values/PTP/embedded platform roadmap is not a desktop RC blocker; vendor-specific acceptance is not universal conformance |
| Product persistence | OFFLINE-TESTED + UI-PROVEN | atomic versioned state; workspace restore; eight-entry deduplicated recent endpoint list; corrupt/unknown state fail-closed recovery | automatic connection on startup is intentionally disabled |
| Diagnostics | OFFLINE-TESTED + UI-PROVEN | bounded activity retention plus atomic diagnostics export with a 2 MiB payload guard | diagnostics export is operational evidence, not a protocol trace substitute |
| Windows package | DEPLOYED/INSTALLED-PROVEN | `windeployqt` staging; portable ZIP; NSIS installer; silent install; staged and installed smoke test; helper server and Qt runtime verification | installer does not silently redistribute Npcap; runtime guidance remains explicit |

## Final Milestone S proof head

Executable/release behavior is proven on head `67ffa58e91fe11e2af7a2de4559e4451a1d5a34d`.

**IED Simulator Release Hardening #10**, run `34811313865`:

- Linux release-soak job `103873063840`: SUCCESS.
- Windows release-package job `103873063622`: SUCCESS.
- `PRODUCT_HARDENING_PASS state=atomic workspace_restore=4 recent=8 dedupe=pass bounded_recent=8 crash_recovery=pass auto_reconnect=false npcap_policy=explicit`
- `PRODUCT_HARDENING_NEGATIVE_PASS corrupt_state=ignored unsupported_schema=rejected invalid_workspace=rejected invalid_endpoint=rejected unbounded_recent=rejected`
- `WINDOWS_RUNTIME_READINESS_PASS npcap_required=true npcap_available=false guidance=pass raw_ethernet_ready=false`
- `MMS_CLIENT_RECONNECT_SOAK_PASS cycles=24 association_reacquire=pass clean_disconnect=pass stale_state_not_applied=true bounded_io_worker=1`
- `APPLICATION_CLOSE_SOAK_PASS cycles=12 runtime_started=pass about_to_quit_cleanup=pass orphan_children=0 socket_release=pass`
- `WINDOWS_RELEASE_PACKAGE_PASS staged_smoke=pass installed_smoke=pass qt_runtime=pass helper_server=pass npcap_guidance=pass installer=nsis portable=zip reconnect_cycles=24`

**IED Simulator Qt #565**, run `34811313883`, job `103873063746`: SUCCESS through the complete retained desktop regression tail. Key release markers include:

- `IEDSIM_GUARDRAILS_PASS output_cap=65536 drain_budget=64 retained=129 flood_dropped=2031616 diagnostics_export=atomic`
- `SCL_WORKSPACE_PASS edition_source=ed2 exact_preserve=pass canonical=pass dtt=pass references=pass configured_values=pass deterministic=pass ed1=pass ed2=pass ed21=pass scd=pass icd=pass cid=pass semantic_roundtrip=pass ieds=1 lns=4 leaves=23`
- `RUNTIME_LIFECYCLE_PASS cycles=12 ... started=12 finished=12 delayed_kill_guard=pass`
- `RUNTIME_SCALE_RESPONSIVENESS_PASS large=20000,50000 multi_ied=3 fleet_rollback=pass nonblocking_clear=pass`
- `GOOSE_WIRE_INTEROP_PASS appid=4097 vlan=100 members=3 st_initial=1 sq_retx=1 st_change=2 ttl_initial=8 ttl_retx=16 pcap_packets=3`
- `GOOSE_WORKSPACE_PASS ... pcap_export=pass`
- `LIVE_RUNTIME_DATA_PLANE_PASS cases=1000,10000 manifest_hot_rewrites=0 bounded_pending=256 bounded_inflight=256`
- `IEDSIM_GUI_LIVE_VALUE_PASS ... control_direct_normal=pass control_sbo_normal=pass control_direct_enhanced=pass control_sbo_enhanced=pass urcb_gi=pass brcb_event=pass`
- `MULTI_IED_PASS` for same TCP port on distinct loopback addresses.

**C++ CI #1501**, run `34811313919`: Windows/MSVC job `103873063736`, Linux/Clang job `103873063924`, and Linux/GCC job `103873063955` all SUCCESS.

All twelve workflows associated with executable proof head `67ffa58e91fe11e2af7a2de4559e4451a1d5a34d` completed SUCCESS: IED Simulator Release Hardening #10, IED Simulator Qt #565, C++ CI #1501, MMS R1-R2 Server CI #165, Security and Evidence #1476, external IEC 61850 client Parity Server CI #463, Control Interop Harness CI #753, Dynamic RCB Trial Harness CI #792, BRCB Hard Profile CI #749, Embedded Profile CI #1146, ARStack Studio Qt #521 and SMV Injector GUI #334.

Documentation-only commits after this proof head do not reopen Milestone S unless executable/runtime behavior changes.

## Runtime and shutdown release gates

The release-candidate behavior head passes all of the following:

- `PRODUCT_HARDENING_PASS`
- `PRODUCT_HARDENING_NEGATIVE_PASS`
- `WINDOWS_RUNTIME_READINESS_PASS` on the Windows release job
- `MMS_CLIENT_RECONNECT_SOAK_PASS cycles=24`
- `APPLICATION_CLOSE_SOAK_PASS cycles=12 ... orphan_children=0 socket_release=pass`
- `WINDOWS_RELEASE_PACKAGE_PASS ... staged_smoke=pass installed_smoke=pass qt_runtime=pass helper_server=pass npcap_guidance=pass installer=nsis portable=zip`
- retained `RUNTIME_LIFECYCLE_PASS`, large-SCL performance, responsiveness, GOOSE, live-data, control, URCB/BRCB and multi-IED gates from `IED Simulator Qt`.

The application-close soak is intentionally process-external. Each cycle starts the real Qt workbench and its MMS helper, proves the configured endpoint becomes live, allows normal application exit, then rejects any orphan helper process or unreleased listen socket.

## Windows / Npcap policy

The Windows application checks for both `wpcap` and `Packet` runtime libraries. If they are unavailable, the application remains usable for MMS/SCL/simulator workflows and explicitly reports that Npcap must be installed before Windows raw-Ethernet GOOSE Monitor/Publisher use. The final Windows runner intentionally had no Npcap runtime; the release gate proved `guidance=pass` and `raw_ethernet_ready=false` rather than manufacturing a false-positive raw-Ethernet result.

The release package does **not** bundle or silently redistribute Npcap.

## External-vendor interoperability status

The stable-release candidate now includes **vendor-specific external interoperability evidence** from external vendor external IEC 61850 client using the reference IEC 61850 model CID profile exercised throughout the parity work. The retained diagnostic proves full model discovery, URCB/BRCB reporting, SBO Enhanced Open/Close with external IEC 61850 client `ctlNum=0`, positive CommandTermination ordering, process feedback, and BRCB event progression.

This evidence is deliberately scoped. No physical relay was attached to the GitHub runner, and the external vendor/reference IEC 61850 model result is not relabelled as universal multi-vendor interoperability or IEC 61850 conformance certification. Additional vendor hardware/simulator results should be appended as separate profile-specific evidence.

## Known intentional limitations at RC

- MMS remote file mutation (upload/delete/rename): **NOT CLAIMED**.
- Full Setting Group edit transaction: **NOT CLAIMED**.
- Lossless unknown vendor XML/extensions during canonical SCL conversion: **NOT CLAIMED**; exact-source Save As preserves the source bytes.
- Automatic field-device connection after application restart: intentionally disabled.
- Windows raw Ethernet without Npcap: unavailable by design and surfaced with operator guidance.
- Universal multi-vendor interoperability: **NOT CLAIMED**. external vendor external IEC 61850 client + the tested reference IEC 61850 model CID profile is externally evidenced; other vendor/device profiles require their own acceptance evidence.
- Sampled Values runtime simulation, PTP, ESP/embedded and broader process-bus platform work remain separate ARStack platform tracks and are not desktop RC prerequisites.

## Packaging artifacts

`IED Simulator Release Hardening #10` produced Windows RC artifact `arstack-iec61850-workbench-windows-rc` from behavior head `67ffa58e91fe11e2af7a2de4559e4451a1d5a34d`.

- GitHub Actions artifact ID: `10334848336`
- Size: `60,552,499` bytes
- Digest: `sha256:cbb81b61a596a46d3e8882d842057b6a71c96a9005cdb6a1e393d56c76d7e28c`
- Artifact retention expiry: `2026-09-28T06:02:54Z`

The artifact contains:

1. `ARStack-IEC61850-Workbench-Setup-win64.exe` — installable NSIS package.
2. `ARStack-IEC61850-Workbench-portable-win64.zip` — portable deployed tree.

Both contain the workbench executable, simulator helper executable and Qt runtime/QML/plugin deployment produced by `windeployqt`. The workflow smoke-tested the staged application and a silent-installed copy before upload.
