# arstack61850 Feature Matrix

Status terms are intentionally conservative:

- **Live-proven** — exercised against a reachable IED/vendor simulator endpoint.
- **Offline-tested** — deterministic unit/integration/fuzz/CI evidence exists, but no current live-device claim is made.
- **Software-ready for lab** — the production path and guarded live-test harness are implemented; retained physical/simulator evidence is still required.
- **Partial** — useful implementation exists but important parity/interop gaps remain.
- **Planned** — not implemented as a usable public feature yet.

| Area | Feature | Status | Current evidence / boundary |
|---|---|---|---|
| Transport | TCP client transport | Live-proven | Windows/POSIX transport is used by live MMS discovery and is the transport under the C5 control adapter. |
| OSI | TPKT / COTP | Live-proven | Live association path reaches MMS endpoints; controlled timeout/recovery evidence has been accepted on OCR7SR12. |
| OSI | Session / Presentation / ACSE | Live-proven | Current association profiles and the C#-compatible fallback path interoperate with the exercised endpoint. |
| MMS client | Initiate / association lifecycle | Live-proven | Fresh association/reconnect cycles are accepted on the exercised OCR7SR12 evidence set. |
| MMS client | GetNameList domains/variables | Live-proven | OCR7SR12 evidence includes large-model pagination with final `moreFollows=false`. |
| MMS client | GetVariableAccessAttributes | Live-proven | 123/123 LN-root probes and exact projected MMS types across 6,990 canonical DA on the exercised endpoint. |
| MMS client | Read | Live-proven | Full OCR7SR12 RCB inventory represented 286/286 with zero NotRead/ReadFailed in the accepted read-only snapshot. |
| MMS client | Write codec/runtime | Offline-tested / live-control lab pending | Confirmed multi-variable Write is implemented; C5 routes control Writes through the production association runtime. Physical control acceptance is still pending. |
| MMS client | GetNamedVariableListAttributes | Live-proven/optional | Used when DataSet directory reads are enabled. |
| MMS client | GO/SV/SG/LG exact-attribute deep Read | Live-proven | OCR7SR12 SGCB deep read accepted 5/5 attributes without mutation. |
| Live model | LD/LN/DO/DA projection | Live-proven | OCR7SR12: 4 LD, 123 LN, 1,186 DO, 6,990 DA; C#↔C++ structural comparison blocking=0. |
| Live model | IED identity resolver | Live-proven | OCR7SR12 identity inferred from live logical-device evidence. |
| Live model | CDC inference | Live-proven + offline-tested | Conservative C#-oracle-derived inference; exact command binding still uses live TypeSpecification. |
| Live model | TypeTemplates | Live-proven | Current C# and C++ captures canonicalize to the same 471 template identities. |
| Live model | Structural fingerprint | Live-proven | Repeated primary-vendor cycles and timeout/recovery retained the accepted structure-only fingerprint. |
| Cross-language | Same-IED C# ↔ C++ structural/type comparator | Live-proven | OCR7SR12 structural and common exact-type comparison pass with zero blocking findings. |
| DataSet | Name inventory/member directory | Live-proven | Dynamic inventory may legitimately vary with runtime configuration. |
| DataSet | Dynamic define/delete | Planned for parity tranche | Generic service foundations exist, but full guarded live lifecycle parity remains later work. |
| Reporting | RCB inventory/read-only probe | Live-proven | Current endpoint exposes 286 RCB identities; accepted full probe classified all without read failures. |
| Reporting | URCB operational runtime | Offline-tested | Reservation, RptEna, GI/touched-state cleanup and event hard profiles are implemented; broad multi-vendor live reporting acceptance remains pending. |
| Reporting | BRCB retained replay / EntryID / PurgeBuf | Offline-tested | Bounded retained history, replay/resume/rewind, PurgeBuf, overflow/replay-gap handling and hard-profile evidence are implemented. |
| Reporting | BRCB Owner / ResvTms / association lifecycle | Offline-tested | Multi-client ownership, reconnect/expiry and association-loss semantics are implemented and hard-profile tested. |
| Reporting | BRCB recovery image v2 | Offline-tested / physical NVM pending | A/B recovery preserves retained window/cursor/gap with v1 restore; physical flash geometry/endurance/power-loss evidence is a separate gate. |
| Control | `ctlModel` discovery | Software-ready for lab | C5 reads live ctlModel and exact control TypeSpecifications before any optional Write. |
| Control | Direct normal | Software-ready for lab | C1-C4 deterministic semantics + C5 guarded live harness; exactly-one `Oper` physical evidence remains required. |
| Control | SBO normal | Software-ready for lab | Normal Select is `SBO` Read; immutable sequence, Oper and explicit Cancel are implemented. Physical SBO lifecycle evidence remains required. |
| Control | Direct enhanced | Software-ready for lab | Oper acceptance remains pending until correlated CommandTermination; ordinary ST/MX reports cannot complete the command. |
| Control | SBO enhanced | Software-ready for lab | One `SBOw` selection Write followed by one `Oper` Write and correlated CommandTermination; physical evidence remains required. |
| Control | `Oper` / `SBOw` / `Cancel` live TypeSpecification binding | Offline-tested | Golden MMS Data vectors cover ctlVal/origin/ctlNum/T/Test/Check and optional operTm; unknown vendor fields fail closed. |
| Control | Control value types | Offline-tested | SPC/DPC/integer/unsigned/floating/step-position binding is conservative; DPC uses network bit order. |
| Control | Association ownership / immutable sequence | Offline-tested | Second-client takeover, selection expiry, sequence mismatch, authorization revocation and association-loss cleanup are covered. |
| Control | CommandTermination | Offline-tested | Positive termination is exact `CO/Oper` correlated; enhanced command does not complete at Write acceptance. |
| Control | LastApplError / ControlError / AddCause | Offline-tested | Standard ControlError and AddCause 0..27 mappings plus raw unknown-code preservation are implemented. |
| Control | Automatic command retry | Disabled / CI-proven contract | C1-C5 deliberately perform no automatic retry. C5 records every control Write for reconciliation with PCAP. |
| Control | Live interoperability harness | Software-ready for lab | Read-only by default; exact `--arm IEC61850-LAB-CONTROL` token plus object/action/value is required before Write. JSON evidence and tested CI binaries are produced. |
| GOOSE | PDU / Ethernet encode-decode | Offline-tested | Golden vectors, deterministic tests and fuzz corpus. Physical multi-vendor process-bus acceptance remains separate. |
| GOOSE | Publisher/subscriber runtime foundations | Offline-tested | Physical process-bus acceptance is outside this control tranche. |
| Sampled Values | PDU / Ethernet encode-decode | Offline-tested | Process-bus work is maintained in its separate development/evidence thread; this matrix does not use that work as MMS-control evidence. |
| SCL | Read-only parser / engineering support | Offline-tested | Host engineering surface. Mutable standards-aware export/reconstruction parity remains later work. |
| COMTRADE | CFG/DAT read support | Offline-tested | ASCII/BINARY/BINARY32/FLOAT32 and mapping foundations are implemented. |
| PCAP | Read/write/evidence support | Offline-tested | Used as a retained evidence format; C5 expects Wireshark PCAP/PCAPNG to be reconciled with its JSON control Write log. |
| Embedded | no-RTTI hard profiles | Offline-tested | GCC/Clang embedded/hard-profile gates are maintained. |
| Embedded | no-exceptions full MCU codec | Partial | Some shared legacy validation/convenience paths still carry exception debt. |
| MMS server | Static association/browse/read/write foundations | Offline-tested / partial | Static server-side runtime and hard-profile components exist on the active stack; full deterministic IED simulator/server integration parity remains later work. |
| File services | File directory/read/write / fault records | Planned for next parity work | Not part of C5 control acceptance. |
| Security | ASan/UBSan | CI-gated | Security/Evidence workflow is required on active heads. |
| Security | libFuzzer corpora | CI-gated | BER, GOOSE, SV, PCAP, SCL, COMTRADE, OSI and MMS corpora are maintained. |
| Portability | GCC / Clang / MSVC | CI-gated | C1-C5 production/control paths build through the normal cross-platform matrix; C5 also has its own three-platform harness CI. |
| Acceptance | Read-only same-IED structural stability | Live-proven | Primary-vendor repeated-cycle and timeout/recovery evidence accepted on OCR7SR12. |
| Acceptance | Control C5 software gate | Software-ready for lab | Guarded harness, runbook, JSON evidence schema, no-retry accounting and CI binaries are implemented. |
| Acceptance | Physical IEC 61850 control interoperability | Pending | Direct normal, SBO normal, Direct enhanced, SBO enhanced, safe negative diagnostic and association-loss evidence must be retained per `CONTROL_INTEROP_RUNBOOK.md`. |
| Acceptance | Multi-vendor physical interoperability | Not yet complete | Required before industrial replacement or broad conformance claims. |

## Current live mutation boundary

The established Phase 4C/4C.1 discovery tools remain read-only and may issue GetNameList,
GetVariableAccessAttributes, GetNamedVariableListAttributes and Read only.

Phase 4D-C introduces a separate explicit active-control API. The C5 interoperability harness
preserves the safe default: without an action it performs discovery only, and any Write is
rejected unless the caller supplies the exact laboratory arm token plus the target object,
action and value. It never performs an automatic command retry.

Reporting/BRCB mutation semantics are implemented in their bounded server/runtime hard profiles,
but broad live multi-vendor reporting acceptance is distinct from the C5 control gate.

## Current control milestone

C1-C4 provide software parity for the guarded control workflow and are integrated into
`ARIEC61850::core`. C5 adds the live laboratory harness and retained evidence contract.
The software state is therefore:

`SOFTWARE_READY_FOR_LAB`

A specific IED/control-model profile becomes:

`LAB_INTEROP_PASSED`

only after the JSON write count/reference evidence agrees with retained PCAP/PCAPNG and the
required positive/negative/association-loss behavior is accepted on an isolated laboratory IED
or simulator. Neither state is IEC 61850 conformance certification.

## Next parity direction after C5 evidence

The next highest-value migration gaps are MMS file/fault-record transfer, dynamic DataSet
lifecycle, mutable SCL export/reconstruction, deterministic IED simulator/server integration,
and then broader application/UI parity. Sampled Values / ESP32-P4 process-bus development stays
separate from this MMS-control tranche.
