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
- Setting Group support currently proves discovery/deep-read (including ActSG, CnfEdit, EditSG, LActTm and NumOfSG on retained OCR7SR12 evidence), which is not equivalent to a complete edit/activate workflow.
- SCL parsing recognizes the supported edition model, but mutable SCL workspace/export and live-discovery-to-SCL reconstruction remain explicit parity gaps.

## Scope rule after Milestone M

After M, choose work by **IEDScout product impact**, not by breadth of the IEC 61850 platform.

Do **not** make Sampled Values runtime simulation, PTP, ESP32/embedded work, or broader process-bus instrumentation prerequisites for the desktop IEDScout-alternative release. Those remain valuable ARStack platform capabilities on their own track.

The priority order is:

**M GOOSE TX ✅ -> N MMS Client/Discovery ✅ -> O RCB/Reports -> P GOOSE Monitor/Publisher -> Q Files + Setting Groups -> R SCL Export/Ed1-Ed2 -> S Release Hardening**

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

## Milestone O — Report / RCB Commissioning — NEXT

Turn the existing reporting capability into technician workflow rather than another protocol tranche.

### Required surface

- DataSet browser with ordered members and canonical references;
- URCB/BRCB inventory and selected RCB inspector;
- static and dynamic candidate paths surfaced explicitly;
- reservation/ownership state, enable/disable, GI, trigger options, BufTm, IntgPd, RptID, ConfRev and DataSet binding;
- received-report stream with reason-for-inclusion, sequence/EntryID/status where available;
- BRCB replay/resume/rewind/overflow indicators where supported by the canonical runtime;
- safe cleanup on disable/disconnect/association loss;
- no automatic RCB mutation/failover after an ambiguous mutation failure.

### Definition of Done

A technician can discover, configure/enable an eligible report path, request GI, observe live reports, and cleanly release it from one Reports workspace, while the existing guarded static/dynamic RCB safety semantics remain intact.

## Milestone P — GOOSE Workspace: Monitor + Publisher

Do not ship GOOSE TX and GOOSE sniffing as unrelated tools. Combine them into one workspace.

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

## Milestone Q — File Transfer + Setting Groups

### Files

Minimum product parity starts from capability already proven:

- directory tree/list;
- path, size/date metadata where provided;
- streaming download;
- progress, cancel and precise error/cleanup presentation;
- bounded directory continuation/pagination.

Upload/delete/rename are **not assumed requirements**. Audit target-product parity and field need first; implement remote mutation only if it materially improves the intended engineering workflow and can be guarded safely.

### Setting Groups

First expose the SGCB accurately:

- NumOfSG;
- ActSG;
- EditSG;
- CnfEdit;
- LActTm.

Then add guarded **Activate Group** if the canonical write/type path supports the target safely.

Only claim full Setting Group editing after the complete transaction is implemented and proven:

`EditSG -> edit SE values -> CnfEdit`

Deep-read evidence alone must never be labelled full Setting Group support.

## Milestone R — SCL Reconstruction / Export / Save As

This is a real product gap and should be isolated because it is riskier than simple serialization.

### Required surface

- construct a mutable engineering workspace from canonical discovery/SCL data without losing references silently;
- deterministic ICD/CID export as applicable to available information;
- Save As supported SCL editions, including explicit Ed1/Ed2 conversion policy where supported;
- deterministic DataTypeTemplates/reference generation;
- preserve or explicitly report Services, Communication, control blocks and vendor extensions that cannot be represented safely;
- namespace/version handling must be explicit;
- unsupported or ambiguous conversions fail closed rather than fabricating data.

### Definition of Done

Exported files reparse successfully, preserve the modeled semantics covered by the source workspace, and pass deterministic round-trip/reference validation. No claim of lossless vendor conversion is made without evidence.

## Milestone S — IEDScout Product Hardening / Release Candidate

Unify the product rather than adding another protocol feature.

### Product hardening

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