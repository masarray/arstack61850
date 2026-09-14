# IEDScout Product Parity Roadmap

This roadmap defines the desktop-product track for turning existing ARStack IEC 61850 capabilities into a practical engineering workbench comparable in daily workflow to IEDScout.

It is intentionally separate from the broader ARStack platform roadmap. ARStack may continue to advance Sampled Values, PTP, embedded/ESP targets, process-bus instrumentation, and other protocol/platform work, but those items are **not blockers** for the IEDScout-alternative desktop product.

## Product objective

Ship one desktop engineering application that surfaces the mature ARStack protocol/runtime capabilities through a coherent technician workflow instead of continuing protocol development feature-by-feature.

The application should converge toward one workbench with these top-level workspaces:

- **IED Connection** — connect, associate, discover, browse, read/write, inspect live model;
- **Reports** — DataSet, URCB/BRCB, static/dynamic RCB commissioning and received reports;
- **GOOSE** — Monitor and Publisher in one workspace;
- **Files** — remote MMS file browsing/download and later only the mutation features justified by parity needs;
- **Settings** — Setting Group inspection and guarded activation/edit transactions;
- **SCL** — reconstructed/live engineering model, export and Save As;
- **Simulator** — the existing A-M simulator/fleet work, retained in the same desktop application rather than split into another product.

Protocol/core code remains modular. The product UX should not expose the user to library boundaries that are irrelevant to commissioning work.

## Current leverage and confirmed gaps

The product roadmap is based on the current repository state, not on rewriting IEC 61850 from scratch:

- MMS association, GetNameList, exact type discovery, Read, guarded Write, DataSet discovery and live LD/LN/DO/DA projection are already substantial foundations for a client workbench.
- Dynamic and static RCB paths already include strong inventory/planning/session/GI/lifecycle evidence. BRCB retained replay, EntryID/PurgeBuf/overflow, Owner/ResvTms and cleanup semantics also exist at various live/offline evidence levels.
- Live discovery already exposes DataSet inventory and GOOSE/SV/SettingGroup/Log control-block identities with exact-attribute deep reads where implemented.
- Stage M closes the desktop simulator's real Layer-2 GOOSE publication path with explicit NIC binding, SCL addressing, sequencing/retransmission semantics, bounded lifecycle and PCAP evidence.
- MMS file service is currently read-oriented: FileDirectory + FileOpen/FileRead/FileClose download. Upload/delete/rename are not currently implemented.
- Setting Group support now includes SGCB discovery/deep-read and guarded ActSG activation/verification, but full edit is intentionally not claimed until `EditSG -> edit SE values -> CnfEdit` exists end-to-end.
- SCL now supports exact-source preservation plus deterministic canonical reconstruction/export for modeled semantics, explicit Ed1/Ed2/Ed2.1 conversion, and guarded SCD/ICD/CID output. Canonical conversion intentionally does **not** claim lossless vendor-extension preservation; exact-source Save As remains the lossless path for the original source bytes.
- Release hardening is now the active desktop-product gap: persistence/recovery and runtime-readiness work has started, while Windows packaging/installer, release-evidence classification and final release-candidate soak/interoperability remain open.

## Scope rule after Milestone M

After M, choose work by **IEDScout product impact**, not by breadth of the IEC 61850 platform.

Do **not** make Sampled Values runtime simulation, PTP, ESP32/embedded work, or broader process-bus instrumentation prerequisites for the desktop IEDScout-alternative release. Those remain valuable ARStack platform capabilities on their own track.

The priority order is:

**M GOOSE TX ✅ -> N MMS Client/Discovery ✅ -> O RCB/Reports ✅ -> P GOOSE Monitor/Publisher ✅ -> Q Files + Setting Groups ✅ -> R SCL Export/Ed1-Ed2 ✅ -> S Release Hardening IN PROGRESS**

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

This milestone changes the application from a simulator-centric tool into an engineering client while retaining the existing simulator as a peer workspace.

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
- deterministic loopback QA using the real static MMS compatibility runtime (`MmsStaticConnectionRuntime` / `MmsStaticServerSession`) and an explicitly writable SP fixture, proving discovery, exact-type Read, guarded Write + verification Read, and stale-session rejection without making normal simulator manifest leaves globally writable;
- the existing A-M simulator automation and `--scl` path remain intact under the `Simulator` workspace.

### Definition of Done

Milestone N is closed only when the normal `IED Simulator Qt` workflow passes `MMS_CLIENT_WORKBENCH_PASS`, `MMS_CLIENT_STALE_SESSION_NEGATIVE_PASS`, QML smoke, and all retained simulator A-M regressions on the same final implementation head. Closure evidence is recorded on PR #82.

### Closure evidence

Milestone N implementation head `9cc87444c5c48c45785f401dbee490966558d8bf` passed **IED Simulator Qt #515** on successful rerun attempt 2, run `34757769779`, job `103725871734`.

- `MMS_CLIENT_WORKBENCH_PASS ld=1 ln=1 do=1 da=1 read=7 write_verified=9 exact_type=integer fc=SP persistent_association=pass`
- `MMS_CLIENT_STALE_SESSION_NEGATIVE_PASS stale_generation=1 current_generation=2 stale_state_not_applied=true bounded_io_worker=1`
- Build and QML smoke passed on the same implementation head.
- The same successful rerun retained all A-M simulator regression gates: malformed-input fail-closed, lifecycle soak, 5k/20k/50k SCL performance, runtime responsiveness, commissioning K/L, GOOSE M wire/negative proof, live-data J positive/negative proof, visual regression, MMS-visible GUI state including direct/SBO normal/enhanced control, URCB/BRCB, and multi-IED coexistence.
- **MMS R1-R2 Server CI #139**, run `34757769750`, passed Linux GCC, Linux Clang, and Windows MSVC on the same implementation head.
- All other head workflows relevant to this branch were green at closure: C++ CI, Security and Evidence, IEDScout Parity Server, Control Interop, Dynamic RCB, BRCB hard profile, Embedded Profile, and SMV Injector GUI.

Documentation-only closure commits after `9cc87444c5c48c45785f401dbee490966558d8bf` do not reopen the proven implementation claim unless they change executable/protocol behavior.

## Milestone O — Report / RCB Commissioning — CLOSED

Turn the existing reporting capability into technician workflow rather than another protocol tranche.

### Closed capability

- DataSet browser with ordered members and canonical references;
- URCB/BRCB inventory and selected RCB inspector;
- static and dynamic candidate paths surfaced through the canonical `MmsRcbPoolSelector`;
- reservation/ownership state plus explicit Enable, Enable + GI, Disable/Release and Retry Cleanup actions;
- RptID, DatSet, ConfRev, BufTm, IntgPd, SqNum, RptEna, Resv/ResvTms/Owner, TrgOps and OptFlds inspection;
- received-report stream with decoded values, reason-for-inclusion, sequence, EntryID, overflow and duplicate/gap/reset indicators;
- BRCB EntryID/PurgeBuf capability and overflow indicators surfaced where present without claiming replay/rewind mutation support that is not exposed by the reusable client runtime;
- bounded one-worker client I/O and non-fatal bounded idle polling through `MmsAssociationRuntime::try_poll_once_for()`;
- strict single-candidate RCB policy, polling fallback disabled, and no automatic mutation/failover after ambiguous failure;
- canonical URCB lifecycle reused end-to-end: probe, reserve, enable, GI, disable/release and cleanup-required handling;
- active reconnect QA proving the previous RptEna/Resv ownership is cleaned before association replacement by reacquiring the same URCB and receiving GI again.

### Definition of Done

A technician can discover, configure/enable an eligible report path, request GI, observe live reports, and cleanly release it from one Reports workspace, while the existing guarded static/dynamic RCB safety semantics remain intact.

### Closure evidence

Milestone O product implementation landed in `c8f33e1259cf066de3b359db30eda41726345292`; the strict explicit-URCB policy correction is `ceb5fd021523643b4ca63d197a8bb102b2a52993`, and the final lifecycle proof head is `f2cca6971e6697365e99535ad187a7dacc8e12a4`.

That final head passed **IED Simulator Qt #531**, run `34786507802`, job `103802862092`:

- `REPORTS_WORKBENCH_PASS datasets=2 rcbs=2 static_candidates=2 dynamic_candidates=2 members=3 gi_reports=1 urcb=pass brcb_inventory=pass entryid_indicator=pass cleanup=pass active_reconnect_cleanup=pass`
- `REPORTS_WORKBENCH_NEGATIVE_PASS no_connection_enable=rejected inactive_disable=rejected invalid_selection=rejected strict_single_candidate=true idle_poll_nonfatal=true reconnect_reacquire=true`
- Build, QML smoke, MMS client N, SCL import/fail-closed, lifecycle, large-SCL, responsiveness, commissioning K/L, GOOSE M/P, live-data, visual regression, direct/SBO normal/enhanced control, URCB/BRCB and multi-IED regressions all passed in the same Qt job.
- The earlier Qt #529 SBO-enhanced timeout did not reproduce on the unchanged control harness; #531 completed the full control/MMS regression successfully.
- **MMS R1-R2 Server CI #147** passed Linux GCC, Linux Clang and Windows MSVC.
- All other final-head workflows were green: C++ CI #1483 (GCC/Clang/MSVC), Security and Evidence #1458, IEDScout Parity Server CI #445, Control Interop Harness CI #735, Dynamic RCB Trial Harness CI #774, BRCB Hard Profile CI #731, Embedded Profile CI #1128 and SMV Injector GUI #315.

Documentation-only closure commits after `f2cca6971e6697365e99535ad187a7dacc8e12a4` do not reopen O unless executable/protocol behavior changes.

## Milestone P — GOOSE Workspace: Monitor + Publisher — CLOSED

Milestone P was implemented ahead of O at the user's request; closure is recorded here after final O validation so the roadmap history remains explicit.

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

### Definition of Done

A user can monitor real GOOSE traffic and operate configured test publishers from the same workspace, with independent capture/decode evidence and deterministic interface lifecycle.

### Closure evidence

Milestone P implementation head `94bbf0a707ace442da1f0c4b0c18ab806f873886` passed **IED Simulator Qt #523**, run `34759978848`, job `103730994758`, including the complete retained tail through MMS visibility and multi-IED coexistence.

- `GOOSE_WORKSPACE_PASS pcap_packets=3 streams=256 appid=0x1001 values=decoded duplicate=pass gap=pass regression=pass state_jump=pass state_regression=pass timeout=pass pcap_export=pass`
- `GOOSE_MONITOR_NEGATIVE_PASS malformed=rejected oversize=rejected capacity=rejected missing_interface=rejected explicit_binding=required bounded_streams=256 bounded_members=256 bounded_events=256 retained_packets=4096`
- The Stage M wire gate remained green: `GOOSE_WIRE_INTEROP_PASS appid=4097 vlan=100 members=3 st_initial=1 sq_retx=1 st_change=2 ttl_initial=8 ttl_retx=16 pcap_packets=3` and `GOOSE_PUBLICATION_NEGATIVE_PASS empty_interface=rejected nonexistent_interface=rejected explicit_binding=required`.
- Final O proof head `f2cca6971e6697365e99535ad187a7dacc8e12a4` reran the same GOOSE wire and workspace gates successfully in Qt #531, proving P remained green after the Reports work.

Documentation-only closure commits after the proven P/O heads do not reopen P unless executable/protocol behavior changes.

## Milestone Q — File Transfer + Setting Groups — CLOSED

### Closed Files capability

- one persistent MMS utility association shared by Files and Settings instead of a GUI-specific protocol stack;
- bounded FileDirectory continuation/pagination with remote path, size and modified metadata where supplied;
- streaming FileOpen/FileRead/FileClose download through canonical `MmsFileTransferRuntime`;
- progress and cancellation with stop-token propagation;
- deterministic FileClose/partial-output cleanup on failure;
- upload/delete/rename remain intentionally unexposed because the canonical core is read-oriented and product need has not justified unsafe ad-hoc mutation.

### Closed Setting Groups capability

- SGCB inventory and deep-read of `NumOfSG`, `ActSG`, `EditSG`, `CnfEdit` and `LActTm`;
- guarded Activate Group through the canonical exact-type MMS write path;
- successful activation is followed by verification Read on the same association;
- invalid group numbers and edit-in-progress state fail closed;
- no automatic activation retry after ambiguous write outcome;
- full Setting Group editing is explicitly **not** claimed: `EditSG -> edit SE values -> CnfEdit` remains outside the closed Q surface until that complete transaction is implemented and proven.

### Closure evidence

Milestone Q implementation landed in `8cb7ff3f63e12aaa03aebc54d2356f0a874829d8`; QML empty-SGCB state hardening is `2925f76814101c0b279afc885205eb151e262c25`, and behavior-neutral compiler-warning cleanup is `86fde29fdc22dfcfc5aeb631f4962ce99202e669`.

The final behavioral proof head `2925f76814101c0b279afc885205eb151e262c25` passed **IED Simulator Qt #537**, run `34793801291`, job `103822906491`, through the complete retained tail including SBO-enhanced control and multi-IED:

- `FILE_SETTINGS_WORKSPACE_PASS file_pages=2 file_entries=3 download_bytes=6 file_close=pass sgcb=1 attributes=5 actsg_verified=3 activation_writes=1 persistent_association=pass full_sg_edit_claimed=false`
- `FILE_SETTINGS_WORKSPACE_NEGATIVE_PASS disconnected_ops=rejected invalid_group=rejected edit_in_progress=rejected sink_failure_cleanup=pass bounded_pages=4 bounded_entries=8 no_activation_retry=true`
- `IEDSIM_GUI_LIVE_VALUE_PASS` retained direct/SBO normal/enhanced control, URCB GI and BRCB event proof.
- multi-IED same-port/distinct-address coexistence passed in the same job.
- all ten branch workflows on that behavioral proof head were green: IED Simulator Qt, C++ CI, MMS R1-R2 Server CI, Security and Evidence, IEDScout Parity Server CI, Control Interop Harness CI, Dynamic RCB Trial Harness CI, BRCB Hard Profile CI, Embedded Profile CI and SMV Injector GUI.

The warning-only cleanup head `86fde29fdc22dfcfc5aeb631f4962ce99202e669` then passed **IED Simulator Qt #539**, run `34794848866`, job `103825843204`, through every step including the Q gate, GUI/control regression and multi-IED. Q is therefore closed without claiming unsupported remote file mutation or full SG editing.

Documentation-only commits after the proven Q heads do not reopen Q unless executable/protocol behavior changes.

## Milestone R — SCL Reconstruction / Export / Save As — CLOSED

Milestone R closes the remaining SCL product gap without pretending that normalized reconstruction is byte-for-byte vendor lossless.

### Closed capability

- dedicated top-level SCL workspace backed by canonical `SclParser` / `SclDocument` rather than a GUI-specific parser;
- bounded one-worker source loading with a 64 MiB cap and cancellable operations;
- exact-source Save As that preserves original bytes, rereads them, reparses the output and verifies modeled semantic equivalence;
- deterministic canonical reconstruction/export from `SclDocument` for the semantic surface ARStack actually models;
- deterministic DataTypeTemplates generation for LNodeType, DOType and DAType plus canonical reference generation;
- configured DAI values, DataSet/FCDA, ReportControl, GOOSE, Sampled Values and Communication/address reconstruction where modeled;
- explicit normalized Edition 1, Edition 2 and Edition 2.1 namespace/version policy;
- SCD canonical output plus guarded single-IED ICD/CID output;
- atomic canonical write, reread, reparse, semantic round-trip comparison and repeated-output determinism before an export is labelled verified;
- fail-closed rejection for unsupported profiles, exact-source profile relabel, multi-IED ICD/CID, generic Enum ordinals that cannot be reconstructed safely and nested-SDO ambiguity that the flattened canonical model cannot prove losslessly;
- exact-source preservation remains the vendor-lossless path; canonical reconstruction explicitly reports that unknown vendor XML/extensions are not claimed lossless.

### Definition of Done

Exported files reparse successfully, preserve the modeled semantics covered by the source workspace, and pass deterministic round-trip/reference validation. No claim of lossless vendor conversion is made without evidence.

### Closure evidence

The safe source-preservation foundation spans `b92d2af1a6bff8ef4295c74807cb00f15fa987f0` through `9205c8b4227bb33293ce4fa910c607bd14a7dc97`. Full deterministic reconstruction/export landed in `be7050e6d44d88838f91aeacbe0d4b7282b5adcc`.

Implementation head `be7050e6d44d88838f91aeacbe0d4b7282b5adcc` passed **IED Simulator Qt #551**, run `34798050373`, job `103834905561`, through the complete retained regression tail:

- `SCL_WORKSPACE_PASS edition_source=ed2 exact_preserve=pass canonical=pass dtt=pass references=pass configured_values=pass deterministic=pass ed1=pass ed2=pass ed21=pass scd=pass icd=pass cid=pass semantic_roundtrip=pass ieds=1 lns=4 leaves=23`
- `SCL_WORKSPACE_NEGATIVE_PASS unsupported_profile=rejected exact_relabel=rejected multi_ied_icd=rejected unknown_enum=rejected nested_sdo_ambiguity=rejected malformed=rejected vendor_lossless_claimed=false`
- Build, QML smoke, N/O/Q gates, malformed-input fail-closed, lifecycle, 5k/20k/50k SCL performance, responsiveness, commissioning K/L, GOOSE M/P, live-data, visual regression, direct/SBO normal/enhanced control, URCB/BRCB and multi-IED coexistence all passed in the same Qt job.
- All eleven workflows on the R implementation head were green, including C++ CI #1493. Its Windows/MSVC, Linux/GCC and Linux/Clang jobs all passed.

The first S hardening proof head `fbaeb39445c80e1e111cedbdf52bfacf4ba54d57` then reran the same R gate successfully in **IED Simulator Qt #554**, run `34802981455`, job `103849210782`, including the entire retained tail. R therefore remains closed after release-hardening work begins.

Documentation-only commits after the proven R heads do not reopen R unless executable/SCL behavior changes.

## Milestone S — IEDScout Product Hardening / Release Candidate — IN PROGRESS

Unify the product rather than adding another protocol feature.

### Product hardening target

- coherent navigation across IED Connection, Reports, GOOSE, Files, Settings, SCL and Simulator;
- recent connections and workspace persistence;
- reconnect and crash-safe settings;
- bounded activity/event log and export diagnostics;
- PCAP save/export where relevant;
- Windows installer and Npcap detection/guidance;
- deterministic shutdown of sessions, workers, raw transports and simulator children;
- performance and reconnect/soak testing;
- interoperability evidence against multiple vendor IEDs/simulators where available;
- release documentation that distinguishes live-proven, simulator-proven, offline-tested and not-yet-supported behavior.

### S1 — persistent product state + runtime readiness — PROVEN

The first hardening tranche is `ff3e2a41c53fdb2f5939074e54d41111ae3e1e8c`, followed by Qt 6.8 compatibility correction `fbaeb39445c80e1e111cedbdf52bfacf4ba54d57`.

Closed S1 behavior:

- versioned JSON product state is persisted atomically with `QSaveFile` rather than a partially writable ad-hoc settings file;
- persisted state is capped at 64 KiB and unknown schema, malformed JSON, invalid workspace values, invalid endpoints and unbounded recent lists fail closed to safe defaults;
- selected workspace is restored across application restart;
- recent MMS endpoints are de-duplicated and bounded to eight entries;
- the most recent endpoint is restored into the IED Connection workspace, but startup **does not automatically connect** to a field device;
- the top navigation can select retained endpoints while disconnected;
- Windows runtime readiness explicitly checks both `wpcap` and `Packet` libraries and reports Npcap guidance; non-Windows platforms do not falsely require Npcap;
- state corruption is surfaced as a reset warning and a subsequent valid mutation rewrites a clean atomic state file;
- one-time Simulator auto-selection on explicit SCL import is preserved without overriding a restored product workspace on ordinary startup.

Final S1 proof head `fbaeb39445c80e1e111cedbdf52bfacf4ba54d57` passed **IED Simulator Qt #554**, run `34802981455`, job `103849210782`:

- `PRODUCT_HARDENING_PASS state=atomic workspace_restore=4 recent=8 dedupe=pass bounded_recent=8 crash_recovery=pass auto_reconnect=false npcap_policy=explicit`
- `PRODUCT_HARDENING_NEGATIVE_PASS corrupt_state=ignored unsupported_schema=rejected invalid_workspace=rejected invalid_endpoint=rejected unbounded_recent=rejected`
- the same run passed R again plus the complete retained tail through lifecycle soak, large-SCL performance, responsiveness, commissioning, GOOSE, live-data, screenshot, direct/SBO normal/enhanced control, URCB/BRCB and multi-IED.
- all eleven workflows on the S1 proof head were green: IED Simulator Qt #554, C++ CI #1495, MMS R1-R2 Server CI #159, Security and Evidence #1470, IEDScout Parity Server CI #457, Control Interop Harness CI #747, Dynamic RCB Trial Harness CI #786, BRCB Hard Profile CI #743, Embedded Profile CI #1140, ARStack Studio Qt #515 and SMV Injector GUI #328.

### Remaining S work before release-candidate closure

S is intentionally **not closed** by S1. The remaining release-candidate work includes:

- Windows deploy/package pipeline and installable artifact, including deployed Qt runtime validation;
- installer/runtime Npcap detection and operator guidance proven on Windows rather than inferred only from portable source logic;
- reconnect/application-close soak that spans the unified product workspaces and proves no worker/socket/raw-transport leakage;
- release documentation/evidence matrix that clearly labels live-proven, simulator-proven, offline-tested and unsupported behavior;
- multi-vendor / external simulator interoperability evidence where available, with vendor-specific behavior kept distinct from standard behavior.

### Release principle

A visually complete UI is not a release candidate unless each workspace preserves the underlying core safety/lifecycle contracts and the application survives repeated connect/disconnect/reconnect/open/close workflows without unbounded memory, worker, queue or socket growth.

## Product architecture decision

The existing A-M IED Simulator work is retained as the **Simulator workspace** of the same desktop product.

Do not fork the GUI into many executable tools unless a platform constraint forces it. One application should be able to act as the engineering client while also hosting simulator/fleet workflows for laboratory regression, giving ARStack a useful advantage over a client-only tool.

## Relationship to ARStack platform roadmap

This document is the desktop **product roadmap**.

`MIGRATION_PLAN.md`, `NORTH_STAR.md`, embedded plans, Sampled Values/PTP work and other protocol/platform documents may continue independently. Their completion does not gate Milestones N-S unless a specific desktop parity requirement directly depends on them.

The post-M product strategy is therefore:

**surface existing ARStack capabilities cleanly, close the few real parity gaps, then ship.**
