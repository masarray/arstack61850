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
| Live model | LD/LN/DO/DA projection | Live-proven | Same-IED OCR7SR12 evidence: 4 LD, 123 LN, 1186 DO, 6990 DA; C#↔C++ structural comparison has blocking=0. |
| Live model | IED identity resolver | Live-proven | OCR7SR12 inferred from logical-device domain suffix evidence. |
| Live model | CDC inference | Live-proven + offline-tested | C# registry/pattern port used by live model; exact CDC still depends on available evidence. |
| Live model | TypeTemplates | Live-proven | Live C# and C++ captures canonicalize to the same 471 template identities; raw projection contains 1309 candidates in both current captures. |
| Live model | VariableTypeDiscoveries | Live-proven | C# direct probes and C++ LN-root probes use different request strategies; common projected DA type signatures compare with blocking=0. |
| Live model | Structural fingerprint | Live-proven / stability pending | Current live capture emits a structure-only fingerprint excluding mutable DataSet/RCB runtime state; repeated-cycle stability remains an acceptance gate. |
| Live model | Runtime snapshot fingerprint | Live-proven | Current live capture includes mutable DataSet inventory and RCB binding/enable/reservation evidence separately from structural identity. |
| Cross-language | Same-IED C# ↔ C++ structural comparator | Live-proven | OCR7SR12 structural comparison is PASS with blocking=0 and totalFindings=0; `--types` is also PASS with blocking=0 and totalFindings=0 on common exact type evidence. |
| Cross-language | Same-IED runtime comparator | Live-proven / mutable | Runtime comparison is informational; C++-only `DataSetBindingStatus` is treated as enrichment rather than a false cross-schema mismatch. |
| DataSet | Name inventory | Live-proven | Dynamic inventory may legitimately change between sessions. |
| DataSet | Member directory | Live-proven/optional | Explicitly skipped with `--no-datasets`; empty member list then produces evidence warning, not structural failure. |
| Reporting | RCB inventory | Live-proven | Current endpoint exposes 286 RCB identities: 8 BRCB and 278 URCB in the C# oracle capture. |
| Reporting | Full RCB read-only probe | Live-proven | Current C++ full-probe snapshot classified all 286 RCBs with 0 NotRead and 0 ReadFailed. |
| Reporting | Bound/Unbound runtime semantics | Live-proven | One current snapshot observed 6 Bound and 280 Unbound. This is mutable runtime state, not a permanent device capability claim. |
| Reporting | Dynamic RCB candidate planner | Live-proven | Read-only ranking selected an empty dynamic BRCB candidate on current endpoint. |
| Reporting | Conservative operational availability | Live-proven | Separates dynamic-slot eligibility from populated/static report availability. |
| Reporting | RCB claim/reservation/enable | Planned / out of Phase 4C | No Write, reservation, `RptEna` or GI is authorized by live discovery. |
| Reporting | InformationReport decode / subscription runtime | Offline-tested / partial live integration | Core implementation exists; full live reporting acceptance is not claimed. |
| GOOSE | PDU / Ethernet encode-decode | Offline-tested | Golden vectors, deterministic tests and fuzz corpus. |
| GOOSE | Publisher/subscriber runtime foundations | Offline-tested | Physical multi-vendor process-bus acceptance pending. |
| Sampled Values | PDU / Ethernet encode-decode | Offline-tested | Golden vectors, deterministic tests and fuzz corpus. |
| Sampled Values | Caller-owned bounded encode path | Offline-tested | `encode_into(span)` avoids fresh frame-buffer allocation in steady-state publisher design. |
| Sampled Values | Live MCU publisher | Planned | ESP32-P4-ETH is the future protocol/performance reference after Public Alpha. |
| SCL | Read-only parser / engineering support | Offline-tested | Host-only engineering surface; not part of MCU runtime core. |
| COMTRADE | CFG/DAT read support | Offline-tested | Host engineering feature. |
| PCAP | Read/write/evidence support | Offline-tested | Host evidence feature. |
| Embedded | `ARIEC61850::embedded_core` boundary | Offline-tested | Host-only services excluded by dedicated build profile. |
| Embedded | no-RTTI profile | Offline-tested | GCC/Clang host-simulation gate. |
| Embedded | no-exceptions MCU core | Planned | Legacy protocol error paths still contain exception debt. |
| Embedded | ESP32-P4 native Ethernet adapter | Planned | Target after Public Alpha. |
| Embedded | ESP32-S3 8DI/8DO IED application | Planned | Application reference after bounded MMS server foundation exists. |
| MMS server | Association acceptor + static browse/read model | Planned | Required for the ESP32 I/O IED; not yet a public server feature. |
| MMS mutation | Write/control | Codec/offline foundation only | Live Phase 4C discovery intentionally does not send these operations. |
| File services | File directory/read/write | Planned for later parity | No file-service mutation/request is part of current live acceptance. |
| Security | ASan/UBSan | CI-gated | Security workflow; latest tranche must be green before acceptance claim. |
| Security | libFuzzer corpora | CI-gated | BER, GOOSE, SV, PCAP, SCL, COMTRADE, OSI, MMS services/reporting. |
| Portability | GCC / Clang / MSVC | CI-gated | Required for host Public Alpha. |
| Acceptance | Same-IED repeated structural stability | In progress | Acceptance runner uses `structuralFingerprint`, not mutable legacy canonical fingerprint. Multi-cycle live evidence is the next A1 acceptance tranche. |
| Acceptance | Timeout/reconnect evidence | In progress | Dedicated controlled failure/reconnect runs remain required; successful association cycles alone are not sufficient evidence. |
| Acceptance | Multi-vendor physical interoperability | Not yet complete | Required before full replacement/industrial claims. |

## Current Phase 4C mutation boundary

The live discovery workflow may issue read-only MMS operations such as
GetNameList, GetVariableAccessAttributes, GetNamedVariableListAttributes and
Read. It must not perform Write, control, RCB reservation/enable, GI, dynamic
DataSet mutation, or file-service mutation.

## Current live parity milestone

On the currently exercised OCR7SR12 endpoint, C# and C++ `live-ied-model-v1`
exports now agree structurally on LD/LN/DO/DA, RCB/control-block identities and
canonical type-template projection. Common exact DA MMS type signatures also
compare with zero blocking findings. A separate C++ read-only full-RCB snapshot
classified all 286 RCBs (6 Bound, 280 Unbound at that instant) with zero
NotRead/ReadFailed entries. These facts are evidence for this endpoint and
configuration only; they are not a multi-vendor or full-stack parity claim.

## Public Alpha exit direction

A Public Alpha can be useful before full ARIEC61850 parity if it provides a
reproducible Windows/Linux build, stable read-only live discovery/model JSON,
structural C#↔C++ same-IED comparison, documented GOOSE/SV codecs, and honest
interop evidence. Full server/control/reporting/file/TLS parity is not an A1
requirement.
