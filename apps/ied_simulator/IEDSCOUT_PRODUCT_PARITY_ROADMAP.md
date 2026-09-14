# IEDScout Product Parity Roadmap

This roadmap defines the desktop-product track for turning existing ARStack IEC 61850 capabilities into a practical engineering workbench comparable in daily workflow to IEDScout.

It is intentionally separate from the broader ARStack platform roadmap. ARStack may continue to advance Sampled Values, PTP, embedded/ESP targets, process-bus instrumentation, and other protocol/platform work, but those items are **not blockers** for the IEDScout-alternative desktop product.

## Product objective

Ship one desktop engineering application that surfaces the mature ARStack protocol/runtime capabilities through a coherent technician workflow instead of continuing protocol development feature-by-feature.

The application converges on one workbench with these top-level workspaces:

- **IED Connection** — connect, associate, discover, browse, read/write, inspect live model;
- **Reports** — DataSet, URCB/BRCB, static/dynamic RCB commissioning and received reports;
- **GOOSE** — Monitor and Publisher in one workspace;
- **Files** — remote MMS file browsing/download and only the mutation features justified by parity needs;
- **Settings** — Setting Group inspection and guarded activation/edit transactions;
- **SCL** — reconstructed/live engineering model, export and Save As;
- **Simulator** — the existing A-M simulator/fleet work, retained in the same desktop application rather than split into another product.

Protocol/core code remains modular. The product UX does not expose the user to library boundaries that are irrelevant to commissioning work.

## Current leverage and confirmed boundaries

The product roadmap is based on the repository state, not on rewriting IEC 61850 from scratch:

- MMS association, GetNameList, exact type discovery, Read, guarded Write, DataSet discovery and live LD/LN/DO/DA projection underpin the client workbench.
- Dynamic and static RCB paths include inventory/planning/session/GI/lifecycle evidence; BRCB retained replay, EntryID/PurgeBuf/overflow, Owner/ResvTms and cleanup semantics exist at their explicitly proven evidence levels.
- Live discovery exposes DataSet inventory and GOOSE/SV/SettingGroup/Log control-block identities with exact-attribute deep reads where implemented.
- Stage M closes the desktop simulator's real Layer-2 GOOSE publication path with explicit NIC binding, SCL addressing, sequencing/retransmission semantics, bounded lifecycle and PCAP evidence.
- MMS file service remains read-oriented: FileDirectory + FileOpen/FileRead/FileClose download. Upload/delete/rename are not claimed.
- Setting Group support includes SGCB discovery/deep-read and guarded ActSG activation/verification; full edit remains unclaimed until `EditSG -> edit SE values -> CnfEdit` exists end-to-end.
- SCL supports exact-source preservation plus deterministic canonical reconstruction/export for modeled semantics, explicit Ed1/Ed2/Ed2.1 conversion, and guarded SCD/ICD/CID output. Canonical conversion intentionally does **not** claim lossless vendor-extension preservation; exact-source Save As is the lossless path for original source bytes.
- Release hardening is closed with persistent crash-safe product state, bounded diagnostics export, reconnect/application-close soak, Windows deployed/installed verification, Npcap readiness guidance, cross-platform CI and an explicit evidence ledger.

## Scope rule after Milestone M

After M, choose work by **IEDScout product impact**, not by breadth of the IEC 61850 platform.

Do **not** make Sampled Values runtime simulation, PTP, ESP32/embedded work, or broader process-bus instrumentation prerequisites for the desktop IEDScout-alternative release. Those remain valuable ARStack platform capabilities on their own track.

The completed desktop-product sequence is:

**M GOOSE TX ✅ -> N MMS Client/Discovery ✅ -> O RCB/Reports ✅ -> P GOOSE Monitor/Publisher ✅ -> Q Files + Setting Groups ✅ -> R SCL Export/Ed1-Ed2 ✅ -> S Release Hardening ✅**

## Milestone M — Real GOOSE Publication — CLOSED

Stage M is the bridge from offline GOOSE foundations to the simulator/product publication surface.

Closed capability:

- real Layer-2 GOOSE TX from configured GSEControl + Address;
- explicit Ethernet interface binding with fail-closed behavior;
- APPID, destination MAC, VLAN ID/priority, gocbRef, DataSet, goID and ConfRev from SCL;
- stNum/sqNum/timeAllowedToLive and retransmission sequencing;
- bounded publishers/members, deterministic raw-transport teardown;
- commissioning Start/Stop/status/counters;
- decode-backed PCAP evidence and negative interface-binding QA.

No additional SV milestone follows M on this desktop product track.

## Milestone N — MMS Client & IED Discovery Workbench — CLOSED

### Closed capability

- explicit IP/hostname + TCP port connection profile with Connect, Disconnect and Reconnect state;
- persistent MMS association reused for live discovery and subsequent Read/Write operations;
- canonical live discovery through `MmsTcpLiveDiscoverySession` and `MmsLiveModelBuilder`, with no GUI-specific protocol stack or CLI subprocess;
- typed/virtualized `IED -> LD -> LN -> DO -> DA` tree with expand/collapse and search/filter;
- selected DA inspection for reference, FC, exact MMS type, SCL type, type evidence, value, and sibling quality/timestamp when available;
- selected Read plus bounded visible-range refresh capped at 64 MMS variables per operator action;
- guarded exact-type scalar Write only for FC `SP`, `CF`, `DC`, and `SE`; `ST`, `MX`, `CO`, unknown, structured, array, and unrepresentable values fail closed;
- no automatic Write retry; successful Write is followed by verification Read on the same association;
- one bounded client I/O worker, stop-token cancellation, monotonic session generation, and stale-completion rejection across disconnect/reconnect;
- diagnostic presentation bounded to 128 entries;
- deterministic loopback QA using the real static MMS compatibility runtime and an explicitly writable SP fixture;
- the existing A-M simulator automation and `--scl` path remain intact under the `Simulator` workspace.

### Closure evidence

Milestone N implementation head `9cc87444c5c48c45785f401dbee490966558d8bf` passed **IED Simulator Qt #515**, run `34757769779`, job `103725871734`.

- `MMS_CLIENT_WORKBENCH_PASS ld=1 ln=1 do=1 da=1 read=7 write_verified=9 exact_type=integer fc=SP persistent_association=pass`
- `MMS_CLIENT_STALE_SESSION_NEGATIVE_PASS stale_generation=1 current_generation=2 stale_state_not_applied=true bounded_io_worker=1`
- Build/QML smoke and retained A-M simulator regressions passed on the same implementation head.
- **MMS R1-R2 Server CI #139**, run `34757769750`, passed Linux GCC, Linux Clang, and Windows MSVC.

Documentation-only closure commits after the proven N head do not reopen N unless executable/protocol behavior changes.

## Milestone O — Report / RCB Commissioning — CLOSED

### Closed capability

- DataSet browser with ordered members and canonical references;
- URCB/BRCB inventory and selected RCB inspector;
- static and dynamic candidate paths surfaced through the canonical `MmsRcbPoolSelector`;
- reservation/ownership state plus explicit Enable, Enable + GI, Disable/Release and Retry Cleanup actions;
- RptID, DatSet, ConfRev, BufTm, IntgPd, SqNum, RptEna, Resv/ResvTms/Owner, TrgOps and OptFlds inspection;
- received-report stream with decoded values, reason-for-inclusion, sequence, EntryID, overflow and duplicate/gap/reset indicators;
- BRCB EntryID/PurgeBuf capability and overflow indicators surfaced where present without claiming replay/rewind mutation support that is not exposed by the reusable client runtime;
- bounded one-worker client I/O and non-fatal bounded idle polling;
- strict single-candidate RCB policy, polling fallback disabled, and no automatic mutation/failover after ambiguous failure;
- active reconnect QA proving prior RptEna/Resv ownership is cleaned before association replacement.

### Closure evidence

Milestone O product implementation landed in `c8f33e1259cf066de3b359db30eda41726345292`; strict explicit-URCB policy correction is `ceb5fd021523643b4ca63d197a8bb102b2a52993`, and final lifecycle proof head is `f2cca6971e6697365e99535ad187a7dacc8e12a4`.

That final head passed **IED Simulator Qt #531**, run `34786507802`, job `103802862092`:

- `REPORTS_WORKBENCH_PASS datasets=2 rcbs=2 static_candidates=2 dynamic_candidates=2 members=3 gi_reports=1 urcb=pass brcb_inventory=pass entryid_indicator=pass cleanup=pass active_reconnect_cleanup=pass`
- `REPORTS_WORKBENCH_NEGATIVE_PASS no_connection_enable=rejected inactive_disable=rejected invalid_selection=rejected strict_single_candidate=true idle_poll_nonfatal=true reconnect_reacquire=true`
- retained N and A-M regressions also passed.

Documentation-only closure commits after the proven O head do not reopen O unless executable/protocol behavior changes.

## Milestone P — GOOSE Workspace: Monitor + Publisher — CLOSED

### Monitor

- explicit NIC selection;
- capture EtherType 0x88B8;
- bounded publisher/stream table;
- destination/source MAC, APPID, VLAN, goID, gocbRef, DataSet, ConfRev;
- stNum, sqNum, TTL and live decoded values;
- timeout, duplicate, stale/out-of-order and sequence-reset indicators;
- bounded packet/event retention and optional PCAP save.

### Publisher

- reuse Stage M real Layer-2 publisher and SCL-derived addressing;
- expose configured streams and current publisher runtime state;
- allow deliberate test stimulus through canonical data state, not a duplicate GOOSE-only value store.

### Closure evidence

Milestone P implementation head `94bbf0a707ace442da1f0c4b0c18ab806f873886` passed **IED Simulator Qt #523**, run `34759978848`, job `103730994758`.

- `GOOSE_WORKSPACE_PASS pcap_packets=3 streams=256 appid=0x1001 values=decoded duplicate=pass gap=pass regression=pass state_jump=pass state_regression=pass timeout=pass pcap_export=pass`
- `GOOSE_MONITOR_NEGATIVE_PASS malformed=rejected oversize=rejected capacity=rejected missing_interface=rejected explicit_binding=required bounded_streams=256 bounded_members=256 bounded_events=256 retained_packets=4096`
- Stage M wire/negative gates remained green.

Documentation-only closure commits after the proven P head do not reopen P unless executable/protocol behavior changes.

## Milestone Q — File Transfer + Setting Groups — CLOSED

### Closed Files capability

- one persistent MMS utility association shared by Files and Settings;
- bounded FileDirectory continuation/pagination with remote path, size and modified metadata where supplied;
- streaming FileOpen/FileRead/FileClose download through canonical `MmsFileTransferRuntime`;
- progress and cancellation with stop-token propagation;
- deterministic FileClose/partial-output cleanup on failure;
- upload/delete/rename remain intentionally unexposed.

### Closed Setting Groups capability

- SGCB inventory and deep-read of `NumOfSG`, `ActSG`, `EditSG`, `CnfEdit` and `LActTm`;
- guarded Activate Group through the canonical exact-type MMS write path;
- successful activation followed by verification Read on the same association;
- invalid group numbers and edit-in-progress state fail closed;
- no automatic activation retry after ambiguous write outcome;
- full Setting Group editing is explicitly **not** claimed.

### Closure evidence

Milestone Q implementation landed in `8cb7ff3f63e12aaa03aebc54d2356f0a874829d8`; QML hardening is `2925f76814101c0b279afc885205eb151e262c25`, and warning-only cleanup is `86fde29fdc22dfcfc5aeb631f4962ce99202e669`.

The behavioral proof passed **IED Simulator Qt #537**, run `34793801291`, job `103822906491`; the cleanup head passed **IED Simulator Qt #539**, run `34794848866`, job `103825843204`.

- `FILE_SETTINGS_WORKSPACE_PASS file_pages=2 file_entries=3 download_bytes=6 file_close=pass sgcb=1 attributes=5 actsg_verified=3 activation_writes=1 persistent_association=pass full_sg_edit_claimed=false`
- `FILE_SETTINGS_WORKSPACE_NEGATIVE_PASS disconnected_ops=rejected invalid_group=rejected edit_in_progress=rejected sink_failure_cleanup=pass bounded_pages=4 bounded_entries=8 no_activation_retry=true`

Documentation-only closure commits after the proven Q heads do not reopen Q unless executable/protocol behavior changes.

## Milestone R — SCL Reconstruction / Export / Save As — CLOSED

Milestone R closes the SCL product gap without pretending normalized reconstruction is byte-for-byte vendor lossless.

### Closed capability

- dedicated top-level SCL workspace backed by canonical `SclParser` / `SclDocument`;
- bounded one-worker source loading with a 64 MiB cap and cancellable operations;
- exact-source Save As preserving original bytes and verifying modeled semantic equivalence;
- deterministic canonical reconstruction/export from `SclDocument` for the semantic surface ARStack models;
- deterministic DataTypeTemplates and reference generation;
- configured DAI values, DataSet/FCDA, ReportControl, GOOSE, Sampled Values and Communication/address reconstruction where modeled;
- explicit normalized Edition 1, Edition 2 and Edition 2.1 policy;
- SCD canonical output plus guarded single-IED ICD/CID output;
- atomic write, reread, reparse, semantic round-trip comparison and repeated-output determinism;
- fail-closed rejection for unsupported profiles, exact-source profile relabel, multi-IED ICD/CID, generic Enum ordinals that cannot be reconstructed safely and nested-SDO ambiguity;
- exact-source preservation remains the vendor-lossless path; canonical reconstruction does not claim unknown vendor XML/extensions lossless.

### Closure evidence

The source-preservation foundation spans `b92d2af1a6bff8ef4295c74807cb00f15fa987f0` through `9205c8b4227bb33293ce4fa910c607bd14a7dc97`. Full deterministic reconstruction/export landed in `be7050e6d44d88838f91aeacbe0d4b7282b5adcc`.

Implementation head `be7050e6d44d88838f91aeacbe0d4b7282b5adcc` passed **IED Simulator Qt #551**, run `34798050373`, job `103834905561`:

- `SCL_WORKSPACE_PASS edition_source=ed2 exact_preserve=pass canonical=pass dtt=pass references=pass configured_values=pass deterministic=pass ed1=pass ed2=pass ed21=pass scd=pass icd=pass cid=pass semantic_roundtrip=pass ieds=1 lns=4 leaves=23`
- `SCL_WORKSPACE_NEGATIVE_PASS unsupported_profile=rejected exact_relabel=rejected multi_ied_icd=rejected unknown_enum=rejected nested_sdo_ambiguity=rejected malformed=rejected vendor_lossless_claimed=false`
- complete retained desktop regression also passed.

The first S hardening proof head `fbaeb39445c80e1e111cedbdf52bfacf4ba54d57` reran R successfully in **IED Simulator Qt #554**, run `34802981455`, job `103849210782`.

Documentation-only closure commits after the proven R heads do not reopen R unless executable/SCL behavior changes.

## Milestone S — IEDScout Product Hardening / Release Candidate — CLOSED

Milestone S unifies and hardens the desktop product rather than adding another protocol feature.

### Closed product-hardening capability

- coherent navigation across IED Connection, Reports, GOOSE, Files, Settings, SCL and Simulator;
- versioned crash-safe product state using atomic `QSaveFile`, bounded recent connections, workspace restore and fail-closed corrupt/unknown-state recovery;
- startup deliberately restores endpoint context without automatically connecting to a field device;
- bounded activity/event retention plus atomic diagnostics export capped at 2 MiB;
- PCAP save/export where applicable;
- Windows `windeployqt` staging, portable ZIP and installable NSIS package from one staged tree;
- staged executable and silent-installed executable smoke validation, including deployed Qt/QML/plugins and simulator helper;
- Windows Npcap detection/guidance using both `wpcap` and `Packet` readiness checks; no false raw-Ethernet readiness when Npcap is absent;
- 24-cycle MMS association reacquire/clean-disconnect soak with bounded worker and stale-generation rejection;
- 12-cycle process-external normal application-close soak proving `aboutToQuit` cleanup, zero orphan simulator children and immediate socket reuse;
- retained start/stop/restart lifecycle, large-SCL/reload performance, responsiveness, GOOSE, live-data, control, reports and multi-IED regressions;
- release evidence classified as LIVE-PROVEN, SIMULATOR-PROVEN, OFFLINE-TESTED or NOT CLAIMED rather than upgrading unavailable evidence;
- portability proven across Windows/MSVC, Linux/GCC and Linux/Clang.

### S1 — persistent product state + runtime readiness — PROVEN

The first hardening tranche is `ff3e2a41c53fdb2f5939074e54d41111ae3e1e8c`, followed by Qt 6.8 compatibility correction `fbaeb39445c80e1e111cedbdf52bfacf4ba54d57`.

Final S1 proof head `fbaeb39445c80e1e111cedbdf52bfacf4ba54d57` passed **IED Simulator Qt #554**, run `34802981455`, job `103849210782`:

- `PRODUCT_HARDENING_PASS state=atomic workspace_restore=4 recent=8 dedupe=pass bounded_recent=8 crash_recovery=pass auto_reconnect=false npcap_policy=explicit`
- `PRODUCT_HARDENING_NEGATIVE_PASS corrupt_state=ignored unsupported_schema=rejected invalid_workspace=rejected invalid_endpoint=rejected unbounded_recent=rejected`

### S2/S3 — release soak, Windows packaging, diagnostics and evidence — PROVEN

Release closure work spans:

- `615abd9260a018470d64ed3226cfba91821cf816` — release-hardening workflow, reconnect/application-close soak, Windows packaging and release evidence foundation;
- `65c2bc88dcee326aaffd2c90e9de70db9f6bfc60` — Visual Studio 18/2026 generator correction for the Windows 2025 runner;
- `93ee771064cc290205383da6e4a8ce356f46aced` — atomic bounded Activity Monitor diagnostics export and QA;
- `37a1d5c1e0d57b52661421d03864bbc82d556abd` — explicit NSIS executable resolution;
- `67ffa58e91fe11e2af7a2de4559e4451a1d5a34d` — cross-platform corruption-recovery QA correction; this is the final executable/release behavior proof head.

**IED Simulator Release Hardening #10**, run `34811313865`, passed both final-head jobs:

- Linux release-soak job `103873063840`: SUCCESS.
- Windows release-package job `103873063622`: SUCCESS.
- `WINDOWS_RUNTIME_READINESS_PASS npcap_required=true npcap_available=false guidance=pass raw_ethernet_ready=false`
- `MMS_CLIENT_RECONNECT_SOAK_PASS cycles=24 association_reacquire=pass clean_disconnect=pass stale_state_not_applied=true bounded_io_worker=1`
- `APPLICATION_CLOSE_SOAK_PASS cycles=12 runtime_started=pass about_to_quit_cleanup=pass orphan_children=0 socket_release=pass`
- `WINDOWS_RELEASE_PACKAGE_PASS staged_smoke=pass installed_smoke=pass qt_runtime=pass helper_server=pass npcap_guidance=pass installer=nsis portable=zip reconnect_cycles=24`

The same behavior head passed **IED Simulator Qt #565**, run `34811313883`, job `103873063746`, through every retained desktop gate. Release-specific markers include:

- `IEDSIM_GUARDRAILS_PASS output_cap=65536 drain_budget=64 retained=129 flood_dropped=2031616 diagnostics_export=atomic`
- `PRODUCT_HARDENING_PASS state=atomic workspace_restore=4 recent=8 dedupe=pass bounded_recent=8 crash_recovery=pass auto_reconnect=false npcap_policy=explicit`
- R SCL reconstruction/export remained green;
- lifecycle, 5k/20k/50k SCL performance and reload soak remained green;
- responsiveness, commissioning, GOOSE wire/workspace, live-data, direct/SBO normal/enhanced control, URCB/BRCB and multi-IED coexistence remained green.

**C++ CI #1501**, run `34811313919`, passed Windows/MSVC job `103873063736`, Linux/Clang job `103873063924` and Linux/GCC job `103873063955`.

All twelve workflows associated with behavior proof head `67ffa58e91fe11e2af7a2de4559e4451a1d5a34d` completed SUCCESS: IED Simulator Release Hardening #10, IED Simulator Qt #565, C++ CI #1501, MMS R1-R2 Server CI #165, Security and Evidence #1476, IEDScout Parity Server CI #463, Control Interop Harness CI #753, Dynamic RCB Trial Harness CI #792, BRCB Hard Profile CI #749, Embedded Profile CI #1146, ARStack Studio Qt #521 and SMV Injector GUI #334.

### Windows release artifact

Release Hardening #10 uploaded artifact `arstack-iec61850-workbench-windows-rc`:

- artifact ID `10334848336`;
- size `60,552,499` bytes;
- digest `sha256:cbb81b61a596a46d3e8882d842057b6a71c96a9005cdb6a1e393d56c76d7e28c`;
- contains `ARStack-IEC61850-Workbench-Setup-win64.exe` and `ARStack-IEC61850-Workbench-portable-win64.zip`.

The package does **not** silently bundle Npcap. The final Windows runner intentionally had no Npcap; the workbench correctly reported guidance and kept raw Ethernet unavailable rather than claiming a false pass.

### Evidence boundary at S closure

No third-party vendor IED or proprietary vendor simulator was attached to the GitHub runner. Therefore external multi-vendor field interoperability is explicitly **NOT CLAIMED** rather than fabricated. Internal deterministic simulator interoperability, real MMS socket behavior and real Layer-2 GOOSE encode/publication evidence remain classified according to `RELEASE_EVIDENCE.md`.

Unsupported product surfaces also remain explicit: remote MMS file upload/delete/rename, full Setting Group edit transaction, lossless unknown-vendor XML reconstruction in canonical SCL, and automatic field-device reconnect at startup are not claimed.

Documentation-only commits after executable proof head `67ffa58e91fe11e2af7a2de4559e4451a1d5a34d` do not reopen S unless executable/runtime behavior changes.

### Release principle

A visually complete UI is not a release candidate unless each workspace preserves the underlying core safety/lifecycle contracts and the application survives repeated connect/disconnect/reconnect/open/close workflows without unbounded memory, worker, queue or socket growth. Milestone S closes only because those release-specific gates and the retained protocol/product regression matrix are green together.

## Product architecture decision

The existing A-M IED Simulator work is retained as the **Simulator workspace** of the same desktop product.

Do not fork the GUI into many executable tools unless a platform constraint forces it. One application can act as the engineering client while also hosting simulator/fleet workflows for laboratory regression, giving ARStack a useful advantage over a client-only tool.

## Relationship to ARStack platform roadmap

This document is the desktop **product roadmap**.

`MIGRATION_PLAN.md`, `NORTH_STAR.md`, embedded plans, Sampled Values/PTP work and other protocol/platform documents may continue independently. Their completion does not gate Milestones N-S unless a specific desktop parity requirement directly depends on them.

The post-M desktop product strategy is now complete:

**surface existing ARStack capabilities cleanly, close the real parity gaps, harden the release candidate, then ship.**
