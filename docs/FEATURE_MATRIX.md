# arstack61850 Feature Matrix

Status terms are intentionally conservative:

- **Live-proven** — exercised against a reachable IED or vendor simulator.
- **Offline-tested** — deterministic unit/integration/fuzz/CI evidence exists, but no current live-device claim is made.
- **Software-ready for lab** — the production path and guarded live-test harness are implemented; retained physical/simulator evidence is still required.
- **Partial** — useful implementation exists but important parity or interoperability gaps remain.
- **Planned** — not implemented as a usable public feature yet.

| Area | Feature | Status | Current evidence / boundary |
|---|---|---|---|
| Transport | TCP client transport | Live-proven | Windows/POSIX transport is used by live discovery, reporting, Dynamic DataSet, and C5 control paths. |
| OSI | TPKT / COTP | Live-proven | Live association path reaches MMS endpoints; controlled timeout/recovery evidence exists. |
| OSI | Session / Presentation / ACSE | Live-proven | Current association profiles and the C#-compatible fallback path interoperate with exercised endpoints. |
| MMS client | Initiate / association lifecycle | Live-proven | Fresh association/reconnect cycles are accepted on current evidence sets. |
| MMS client | GetNameList domains/variables | Live-proven | Large live inventories and pagination are exercised. |
| MMS client | GetVariableAccessAttributes | Live-proven | Live model and C5 control discovery consume exact MMS TypeSpecification evidence. |
| MMS client | Read | Live-proven | Discovery, reporting, control descriptor, timeout, and status reads are exercised live. |
| MMS client | Write codec/runtime | Live-proven / guarded simulator | C5 sent explicitly armed `SBOw`, `Cancel`, and `Oper` Writes to IEDScout with JSON/PCAP accounting; physical IED control remains pending. |
| MMS client | GetNamedVariableListAttributes | Live-proven | Dynamic DataSet verification and report binding use the exact ordered directory. |
| MMS client | GO/SV/SG/LG exact-attribute deep Read | Live-proven | OCR7SR12 SGCB deep read accepted 5/5 attributes without mutation. |
| Live model | LD/LN/DO/DA projection | Live-proven | OCR7SR12 structural/type comparison with the C# oracle has zero blocking findings. |
| Live model | IED identity resolver | Live-proven | Identity is inferred conservatively from live logical-device evidence. |
| Live model | CDC inference | Live-proven + offline-tested | CDC inference is conservative; exact command binding uses live TypeSpecification. |
| Live model | Structural fingerprint | Live-proven | Repeated cycles and timeout/recovery retain the structure-only fingerprint. |
| Cross-language | Same-IED C# ↔ C++ structural/type comparator | Live-proven | Structural and common exact-type comparison pass with zero blocking findings on accepted evidence. |
| DataSet | Name inventory/member directory | Live-proven | Dynamic inventory may legitimately vary with runtime configuration. |
| DataSet | Dynamic create/verify/delete | Live-proven / guarded lab | One bounded lifecycle created an owned DataSet, verified exact ordered members, used it for reporting, then deleted only that owned DataSet. |
| Reporting | RCB inventory/read-only probe | Live-proven | Current endpoint evidence includes full RCB inventory and read-only classification. |
| Reporting | Dynamic RCB candidate planner | Live-proven | Read-only ranking identifies safe dynamic candidates without mutation. |
| Reporting | Bounded smart pre-claim failover | Offline-tested / stable live probe | Repeated read-only probes exclude busy/flapping candidates and rerank before mutation; mutation failures never trigger automatic switching. |
| Reporting | Guarded Dynamic RCB lifecycle | Live-proven / guarded lab | Explicitly armed trial bound/enabled one RCB, requested GI, decoded a report, then disabled/unbound with cleanup verification. |
| Reporting | BRCB retained replay / EntryID / PurgeBuf | Offline-tested | Bounded history, replay/resume/rewind, PurgeBuf, overflow, and replay-gap behavior are implemented. |
| Reporting | BRCB Owner / ResvTms / association lifecycle | Offline-tested | Multi-client ownership, reconnect/expiry, and association-loss semantics are hard-profile tested. |
| Reporting | BRCB recovery image v2 | Offline-tested / physical NVM pending | Recovery preserves retained window/cursor/gap with v1 restore; flash endurance and power-loss hardware evidence are separate. |
| Control | `ctlModel` discovery | Live-proven / simulator | C5 rediscovered live `ctlModel` and exact command types before each action on IEDScout. |
| Control | Direct normal | Live-proven / simulator | The retained C5 acceptance record covers OFF and ON with exactly one `Oper` Write per deliberate action. |
| Control | SBO normal | Live-proven / simulator | The retained C5 acceptance record covers Select Read followed by exactly one `Oper`, plus explicit Select/Cancel with no following `Oper`. |
| Control | Direct enhanced | Live-proven / simulator | IEDScout OFF and ON-restore each sent one `Oper`, received positive CommandTermination, and changed/restored status without retry. |
| Control | SBO enhanced | Live-proven / simulator | Retained evidence covers `SBOw -> Oper -> CommandTermination` in both directions, plus a separate accepted `SBOw -> Cancel` case. |
| Control | `Oper` / `SBOw` / `Cancel` live-type binding | Live-proven / simulator | Exact Boolean structures from live GVAA were sent successfully; unknown vendor fields still fail closed. |
| Control | Control value types | Offline-tested | SPC/DPC/integer/unsigned/floating/step binding is conservative; DPC uses network bit order. |
| Control | Ownership / immutable sequence | Offline-tested | Second-client takeover, expiry, mismatch, authorization revocation, and association-loss cleanup are covered. |
| Control | CommandTermination | Live-proven / simulator | Positive enhanced termination is exact-control correlated; ordinary ST/MX reports cannot complete a command. |
| Control | LastApplError / ControlError / AddCause | Live-proven / simulator | IEDScout negative path decoded DataAccessError 3, ControlError 3, and AddCause 8; generic reports require exact origin/ctlNum correlation. |
| Control | Automatic command retry | Disabled / live-proven | Every live case reconciled the exact Write count with PCAP; no automatic retry occurred. |
| Control | Live interoperability harness | Live-proven / simulator | Read-only default, exact arm token, typed values, JSON evidence, PCAP reconciliation, and state restoration are exercised. |
| GOOSE | PDU / Ethernet encode-decode | Offline-tested | Golden vectors, deterministic tests, and fuzz corpus; physical multi-vendor acceptance is separate. |
| GOOSE | Publisher/subscriber runtime foundations | Offline-tested | Physical process-bus acceptance remains pending. |
| Sampled Values | PDU / Ethernet encode-decode | Offline-tested | Process-bus work remains separate from MMS control evidence. |
| Sampled Values | Caller-owned bounded encode path | Offline-tested | `encode_into(span)` avoids fresh frame-buffer allocation in steady-state publisher design. |
| Sampled Values | Live MCU publisher | Planned / embedded path staged | First reference hardware is the Waveshare ESP32-S3-POE-ETH-8DI-8DO; synthetic W5500/ESP-IDF MACRAW transmission proof is required before any timing-grade claim. |
| SCL | Read-only parser / engineering support | Offline-tested | Host engineering surface; mutable standards-aware export remains later work. |
| COMTRADE | CFG/DAT read support | Offline-tested | ASCII/BINARY/BINARY32/FLOAT32 and mapping foundations are implemented. |
| PCAP | Read/write/evidence support | Live-proven for C5 evidence | Npcap loopback captures were reconciled with JSON Write lists and retained SHA-256 hashes. |
| Embedded | `ARIEC61850::embedded_core` boundary | Offline-tested | Host-only services excluded by dedicated build profile. |
| Embedded | no-RTTI hard profiles | Offline-tested | GCC/Clang embedded/hard-profile gates are maintained. |
| Embedded | no-exceptions guarded control core | CI-gated | C1 has a dedicated `-fno-exceptions -fno-rtti` GCC/Clang hard profile. |
| Embedded | no-exceptions full MCU codec | Partial | Shared legacy validation/convenience paths still carry exception debt. |
| Embedded | ESP32-S3 W5500 / ESP-IDF Ethernet adapter | Planned | Reference design uses ESP-IDF W5500 MACRAW integration with raw Ethernet for SV/GOOSE and `esp_netif`/lwIP for MMS TCP on the same interface. |
| Embedded | ESP32-S3 8DI/8DO IED application | Planned | Waveshare board application reference after bounded MMS server foundation exists. |
| MMS server | Static association/browse/read/write foundations | Offline-tested / partial | Static server/runtime components exist; full deterministic simulator/server integration parity remains later work. |
| File services | FileDirectory / FileOpen / FileRead / FileClose client | Live-proven / read-only | Bounded portable streaming runtime, signed FRSM, exact error evidence, rooted-backslash fallback, success cleanup, and forced-failure cleanup were exercised on the controlled lab target. Current directory fit one page; multi-page live continuation remains pending. |
| File services | Upload / delete / rename | Not implemented | Remote file mutation is deliberately outside the current milestone. |
| Security | ASan/UBSan | CI-gated | Security/Evidence workflow is required on active heads. |
| Security | libFuzzer corpora | CI-gated | BER, GOOSE, SV, PCAP, SCL, COMTRADE, OSI, MMS services/reporting, and file-service corpora are maintained. |
| Portability | GCC / Clang / MSVC | CI-gated | C1-C5 build through normal and dedicated matrices; MSVC was validated locally for this integration. |
| Acceptance | Read-only same-IED structural stability | Live-proven | Repeated-cycle and timeout/recovery evidence are retained. |
| Acceptance | Control C5 tested simulator profile | Live-proven | All four ctlModels are recorded in `CONTROL_INTEROP_ACCEPTANCE.md`; the locally retained subset adds hashed JSON/PCAP evidence for negative diagnostics, SBO enhanced Cancel, Direct enhanced termination, exact Write counts, and state restoration. |
| Acceptance | Full physical IEC 61850 control interoperability | Pending | Association-loss, contention, physical-IED, and broader multi-vendor evidence remain required. |
| Acceptance | Multi-vendor physical interoperability | Not yet complete | Required before industrial replacement or broad conformance claims. |

## Current live mutation boundary

Phase 4C/4C.1 discovery remains read-only and sends only explicitly allowed discovery/read services.

Dynamic DataSet/reporting and Phase 4D-C control use separate, explicit mutation APIs. The C5 harness performs discovery only unless an action is supplied, and refuses every control Write without the exact laboratory arm token plus target object and value. It never retries a command automatically.

Reporting/BRCB mutation semantics are distinct from the C5 control gate. Failure in either domain does not authorize automatic mutation or failover in the other.

## Current control milestone

C1-C5 provide a guarded production control path plus a live evidence harness. The tested simulator profile has an acceptance record for all four control models, and a complementary locally retained subset includes hashed JSON/PCAP evidence. The project remains short of physical and broad multi-vendor acceptance.

`SOFTWARE_READY_FOR_LAB` applies to all implemented control models.

`SIMULATOR_INTEROP_PASSED` applies only to the specific simulator profile recorded in `CONTROL_INTEROP_ACCEPTANCE.md`. Any stronger `LAB_INTEROP_PASSED` or physical-IED claim still requires the runbook's retained positive, negative, failure, cleanup, JSON, and packet-capture evidence. These results do not constitute IEC 61850 conformance certification.

## Next parity direction

The next gaps are full remaining control-model/association-loss evidence, MMS file and fault-record transfer, mutable SCL export/reconstruction, deterministic IED simulator/server integration, and broader application/UI parity. Sampled Values and ESP hardware work remain separate from this MMS-control tranche.
