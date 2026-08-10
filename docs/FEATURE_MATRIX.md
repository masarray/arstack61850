# arstack61850 Feature Matrix

Status terms are intentionally conservative:

- **Live-proven** — exercised against a reachable IED/vendor simulator endpoint.
- **Offline-tested** — deterministic unit/integration/fuzz evidence exists, but no
  current live-device claim is made.
- **Partial** — useful implementation exists but important parity/interop gaps remain.
- **Planned** — not implemented as a usable public feature yet.

| Area | Feature | Status | Current evidence / boundary |
|---|---|---|---|
| Transport | TCP client transport | Live-proven | Windows/POSIX transport used by live MMS discovery. |
| OSI | TPKT / COTP | Live-proven | Live association path reaches MMS endpoint. |
| OSI | Session / Presentation / ACSE | Live-proven | Current association profile + tolerant C#-compatible acceptance path interoperates with current test endpoint. |
| MMS client | Initiate / association lifecycle | Live-proven | Read-only discovery session associates successfully. Multi-profile reconnect retry is still incomplete. |
| MMS client | GetNameList domains | Live-proven | Current OCR7SR12 evidence discovers 4 domains. |
| MMS client | GetNameList variables | Live-proven | Thousands of MMS variables discovered live; inventory can vary with exposed runtime/configuration state. |
| MMS client | GetVariableAccessAttributes | Live-proven | C++ LN-root planner completed 123/123 probes and projected exact MMS types across 6990 canonical DA on the current endpoint. |
| MMS client | Read | Live-proven | Full current OCR7SR12 RCB inventory was probed read-only: 286/286 represented, 0 NotRead and 0 ReadFailed in the supplied full-probe snapshot. |
| MMS client | GetNamedVariableListAttributes | Live-proven/optional | Used when DataSet directory reads are enabled; may be deliberately skipped for structural-only runs. |
| MMS client | GO/SV/SG/LG exact-attribute deep Read | SGCB live-proven / integrated path offline-tested | Standalone bounded reader physically read `OCR7SR12PROT/LLN0.SP.SGCB` 5/5 (`ActSG`, `CnfEdit`, `EditSG`, `LActTm`, `NumOfSG`). The new `live_discover --control-block-values` overlay is CI-tested and awaits one integrated physical regression. |
| Live model | LD/LN/DO/DA projection | Live-proven | Same-IED OCR7SR12 evidence: 4 LD, 123 LN, 1186 DO, 6990 DA; C#↔C++ structural comparison has blocking=0. |
| Live model | IED identity resolver | Live-proven | OCR7SR12 inferred from logical-device domain suffix evidence. |
| Live model | CDC inference | Live-proven + offline-tested | C# registry/pattern port used by live model; exact CDC still depends on available evidence. |
| Live model | TypeTemplates | Live-proven | Live C# and C++ captures canonicalize to the same 471 template identities; raw projection contains 1309 candidates in both current captures. |
| Live model | VariableTypeDiscoveries | Live-proven | C# direct probes and C++ LN-root probes use different request strategies; common projected DA type signatures compare with blocking=0. |
| Live model | Structural fingerprint | Live-proven / stability pending | Current live capture emits a structure-only fingerprint excluding mutable DataSet/RCB runtime state; repeated-cycle stability remains an acceptance gate. |
| Live model | Runtime snapshot fingerprint | Live-proven | Current live capture includes mutable DataSet inventory and RCB binding/enable/reservation evidence separately from structural identity. |
| Live model | Control-block runtime evidence overlay | Offline-tested / physical integration pending | Mutable GO/SV/SG/LG values are projected after structural model build, so deep runtime evidence does not change the canonical structural fingerprint. Complete/partial/failed/bounded states are reported explicitly. |
| Cross-language | Same-IED C# ↔ C++ structural comparator | Live-proven | OCR7SR12 structural comparison is PASS with blocking=0 and totalFindings=0; `--types` is also PASS with blocking=0 and totalFindings=0 on common exact type evidence. |
| Cross-language | Same-IED runtime comparator | Live-proven / mutable | Runtime comparison is informational; C++-only `DataSetBindingStatus` is treated as enrichment rather than a false cross-schema mismatch. |
| DataSet | Name inventory | Live-proven | Dynamic inventory may legitimately change between sessions. |
| DataSet | Member directory | Live-proven/optional | Explicitly skipped with `--no-datasets`; empty member list then produces evidence warning, not structural failure. |
| DataSet | Dynamic create/verify/delete lifecycle | Live-proven / guarded lab | One bounded lifecycle created a domain-specific DataSet, verified exact member order, used it for reporting, and deleted only the runtime-owned DataSet. |
| Reporting | RCB inventory | Live-proven | Current endpoint exposes 286 RCB identities: 8 BRCB and 278 URCB in the C# oracle capture. |
| Reporting | Full RCB read-only probe | Live-proven | Current C++ full-probe snapshot classified all 286 RCBs with 0 NotRead and 0 ReadFailed. |
| Reporting | Bound/Unbound runtime semantics | Live-proven | One current snapshot observed 6 Bound and 280 Unbound. This is mutable runtime state, not a permanent device capability claim. |
| Reporting | Dynamic RCB candidate planner | Live-proven | Read-only ranking selected an empty dynamic BRCB candidate on current endpoint. |
| Reporting | Conservative operational availability | Live-proven | Separates dynamic-slot eligibility from populated/static report availability. |
| Reporting | Read-only contention pre-claim probe | Live-proven | Three repeated probes on `OCR7SR12CTRL/BI6GGIO1.urcbA01` remained free/stable and returned `StableProceed`; no claim/write was performed. |
| Reporting | Bounded smart pre-claim failover | Offline-tested / stable live probe | Immediately before mutation, repeated read-only probes reject busy/flapping candidates, add them to the command-local exclusion set, and rerank the next candidate. A stable live candidate passed three probes; a live contended switch remains pending. Mutation failures never trigger automatic switching. |
| Reporting | RCB claim/reservation/enable | Live-proven / guarded lab | Explicitly armed Dynamic RCB trial bound and enabled one BRCB, requested GI, then disabled and unbound it with cleanup verified. Live discovery itself remains read-only. |
| Reporting | InformationReport decode / subscription runtime | Live-proven / single profile | The guarded trial received and decoded an InformationReport through the persistent subscription runtime; long-duration and multi-vendor acceptance remain pending. |
| GOOSE | PDU / Ethernet encode-decode | Offline-tested | Golden vectors, deterministic tests and fuzz corpus. |
| GOOSE | Publisher/subscriber runtime foundations | Offline-tested | Physical multi-vendor process-bus acceptance pending. |
| Sampled Values | PDU / Ethernet encode-decode | Offline-tested | Golden vectors, deterministic tests and fuzz corpus. |
| Sampled Values | Caller-owned bounded encode path | Offline-tested | `encode_into(span)` avoids fresh frame-buffer allocation in steady-state publisher design. |
| Sampled Values | Live MCU publisher | Planned / embedded path staged | First reference hardware is the Waveshare ESP32-S3-POE-ETH-8DI-8DO; synthetic W5500/ESP-IDF MACRAW transmission proof is required before any timing-grade claim. |
| SCL | Read-only parser / engineering support | Offline-tested | Host-only engineering surface; not part of MCU runtime core. |
| COMTRADE | CFG/DAT read support | Offline-tested | Host engineering feature. |
| PCAP | Read/write/evidence support | Offline-tested | Host evidence feature. |
| Embedded | `ARIEC61850::embedded_core` boundary | Offline-tested | Host-only services excluded by dedicated build profile. |
| Embedded | no-RTTI profile | Offline-tested | GCC/Clang host-simulation gate. |
| Embedded | no-exceptions MCU core | Planned | Legacy protocol error paths still contain exception debt. |
| Embedded | ESP32-S3 W5500 / ESP-IDF Ethernet adapter | Planned | Reference design uses ESP-IDF W5500 MACRAW integration with raw Ethernet for SV/GOOSE and `esp_netif`/lwIP for MMS TCP on the same interface. |
| Embedded | ESP32-S3 8DI/8DO IED application | Planned | Waveshare board application reference after bounded MMS server foundation exists. |
| MMS server | Association acceptor + static browse/read model | Planned | Required for the ESP32 I/O IED; not yet a public server feature. |
| MMS mutation | Write/control | Codec/offline foundation only | Live Phase 4C discovery intentionally does not send these operations. |
| File services | FileDirectory / FileOpen / FileRead / FileClose client | Live-proven / read-only | Bounded portable streaming runtime, signed FRSM, exact error evidence, rooted-backslash fallback, success cleanup, and forced-failure cleanup were exercised on the controlled lab target. Current directory fit one page; multi-page live continuation remains pending. |
| File services | Upload / delete / rename | Not implemented | Remote file mutation is deliberately outside the current milestone. |
| Security | ASan/UBSan | CI-gated | Security workflow; latest tranche must be green before acceptance claim. |
| Security | libFuzzer corpora | CI-gated | BER, GOOSE, SV, PCAP, SCL, COMTRADE, OSI, MMS services/reporting/file-service. |
| Portability | GCC / Clang / MSVC | CI-gated | Required for host Public Alpha. |
| Acceptance | Same-IED repeated structural stability | In progress | Acceptance runner uses `structuralFingerprint`, not mutable legacy canonical fingerprint. Multi-cycle live evidence is the next A1 acceptance tranche. |
| Acceptance | Timeout/reconnect evidence | In progress | Dedicated controlled failure/reconnect runs remain required; successful association cycles alone are not sufficient evidence. |
| Acceptance | Integrated control-block deep-read regression | Pending one physical run | Standalone SGCB 5/5 is live-proven; the newly wired `live_discover --control-block-values` path must now reproduce the result on OCR7SR12. |
| Acceptance | Multi-vendor physical interoperability | Not yet complete | Required before full replacement/industrial claims. |

## Current Phase 4C mutation boundary

The live discovery workflow may issue read-only MMS operations such as
GetNameList, GetVariableAccessAttributes, GetNamedVariableListAttributes and
Read. Optional `--control-block-values` performs bounded MMS Read only against
exact GO/SV/SG/LG variable names already returned by live `GetNameList`. It must
not perform Write, control, RCB reservation/enable, GoEna/SvEna mutation, GI,
setting-group mutation, dynamic DataSet mutation, or file-service mutation.

## Current live parity milestone

On the currently exercised OCR7SR12 endpoint, C# and C++ `live-ied-model-v1`
exports now agree structurally on LD/LN/DO/DA, RCB/control-block identities and
canonical type-template projection. Common exact DA MMS type signatures also
compare with zero blocking findings. A separate C++ read-only full-RCB snapshot
classified all 286 RCBs (6 Bound, 280 Unbound at that instant) with zero
NotRead/ReadFailed entries. The standalone deep reader also read the real SGCB
5/5 without mutation. These facts are evidence for this endpoint and
configuration only; they are not a multi-vendor or full-stack parity claim.

## Public Alpha exit direction

A Public Alpha can be useful before full ARIEC61850 parity if it provides a
reproducible Windows/Linux build, stable read-only live discovery/model JSON,
structural C#↔C++ same-IED comparison, documented GOOSE/SV codecs, and honest
interop evidence. Full server/control/reporting/file/TLS parity is not an A1
requirement.
