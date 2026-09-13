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

**M GOOSE TX -> N MMS Client/Discovery -> O RCB/Reports -> P GOOSE Monitor/Publisher -> Q Files + Setting Groups -> R SCL Export/Ed1-Ed2 -> S Release Hardening**

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

## Milestone N — MMS Client & IED Discovery Workbench

This is the highest-impact post-M milestone because it changes the application from a simulator-centric tool into an engineering client.

### Product flow

**Connect to IED -> IP/port -> Associate -> Discover -> browse `IED -> LD -> LN -> DO -> DA` -> Read / guarded Write / inspect live metadata.**

### Required surface

- connection profile with IP, port and explicit connect/disconnect/reconnect state;
- association lifecycle and bounded diagnostics;
- automatic live discovery into a typed Qt model;
- one virtualized tree/projection for LD/LN/DO/DA browsing;
- value, FC, exact type, quality/time where available, writability and reference inspection;
- Read/refresh for selected item and bounded refresh for visible scope;
- guarded Write using exact discovered type; unknown/unrepresentable types fail closed;
- search/filter over reference/name/value/FC/type without rebuilding an unbounded UI tree;
- session generation/revision ownership so stale async discovery/read results cannot overwrite a newer connection;
- reconnect must invalidate old session state deterministically;
- no duplicate complete live model in QML.

### Reuse first

Prefer integration of existing MMS transport/association, live discovery, type probing, Read and guarded Write code. Do not introduce a second protocol implementation for the GUI.

### Definition of Done

- a user can connect to a real/vendor-simulator endpoint and browse the discovered model from the desktop GUI;
- selected live values can be refreshed/read and supported writable values can be changed through the guarded exact-type path;
- disconnect/reconnect and failed discovery do not leave stale UI state;
- large-model navigation/search remains responsive and bounded;
- independent CLI/core comparison or deterministic fixture evidence proves the GUI projection matches the canonical discovered model.

## Milestone O — Report / RCB Commissioning

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
