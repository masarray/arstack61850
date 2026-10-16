# Native IEC 61850 Workbench — physical IED and GUI acceptance

**Scope:** installed Windows C++/Qt `apps/ied_simulator` Workbench, not ARStack Studio/ESP32. A green CI fixture is **not** a measured real-device result. Never mark this document as a passed field test until the named hardware, firmware, installed package SHA, capture and screenshots have been checked.

## 1. Qualifying baseline and reference-screen record

Record **before** editing controls: Workbench `main` or installer commit SHA, installer vs portable ZIP, Windows build, display scaling/resolution, IED vendor/model/firmware, network adapter/driver/Npcap version (if used), test role/authorization and reference screenshot state. Link both ARSAS example and Workbench screenshots for the **same selected IED, RCB, DataSet and operating state**. Visual parity requires side-by-side human review of layout, hierarchy, sections, labels, selection, disabled states, scrolling, colors and values—CI-only QML structure proves neither pixel-perfect appearance nor vendor compatibility.

Official installer evidence: GitHub Actions **IED Simulator Release Hardening** on the *target commit*, artifact `arstack-iec61850-workbench-windows-rc` with setup `.exe` and portable `.zip`. The earlier proven baseline [run #38977806520](https://github.com/masarray/arstack61850/actions/runs/38977806520) was built from `a33271e90fea369abad551e7601b446752e10d54`, **before** the later GUI/GOOSE fixes. Never mistake an older green installer for a new source artifact. For current code, check workflow completion, artifact contents, provenance SHA, and actual installation/launch independently.

## 2. Safe prerequisites and network

- Use a **permitted lab relay**, explicitly approved URCB/BRCB or isolated demo endpoint; obtain operations approval before any `RptEna`, `Resv`, `GI`, DataSet, SG, file write or Control operation. For production/protection relays, default to read-only inspection and passive GOOSE observation.
- Connect the laptop on the correct IED network/VLAN; record intended IPv4/port (usually IEC 61850 MMS TCP 102), adapter binding, route/firewall and VLAN/port-mirror path. Avoid configuring the relay or switch merely to make the application appear successful.
- On Windows, select an **explicit Ethernet NIC** for passive GOOSE. Npcap must be installed with permissions; mirror port/multicast forwarding and VLAN offload may determine whether the PC sees frames. NIC selection is not evidence of an active GOOSE subscription from the publisher.
- Snapshot the existing RCB `RptEna`, `Resv`, `Owner`, `DatSet`, `RptID`, `ConfRev`, `TrgOps`, `OptFlds`, buffer / integrity times and reservation time when exposed. Preserve original ownership and report semantics; NEVER force takeover from another client.

## 3. Step-by-step acceptance matrix

| Surface | Action | Acceptance evidence (record actual state) | Safety and rejection case |
| --- | --- | --- | --- |
| Open SCL | Open SCD/CID, select **exact IED/AP** | LD/LN/DO/DA hierarchy, IED identity, imported edition and selected endpoint match the file | Missing IED/AP, ambiguous identity or incomplete SCL must be visible; do not silently choose another IED |
| Online Browser | Connect and Discover by endpoint | Native MMS association and actual live GetNameList/DataSet/RCB evidence, source marker, elapsed time and confirmed-request count | Timeout/denied services show diagnostics; structural readability is **not** a received DA value |
| Data Model | Select DA, explicit **Read** when permitted | Actual MMS value with typed IEC Quality, TimeQuality/IED timestamp only if present, FC, acquisition/provenance | `NOT READ`, `NOT EXPOSED`, `READ FAILED`, stale/offline and actual false/empty must remain distinguishable |
| DataSets | Select static DataSet | Member count, order, full typed MMS FCDA identity, no duplicated or invented signals | Unavailable, changed or partial DataSet identity must not be used to label received report values |
| Reports | Select actual URCB/BRCB | ReportID, DatSet, ConfRev, effective TrgOps/OptFlds **read-only bit grids with verified/not-read/not-exposed/read-failed status**, Owner/Resv/RptEna fields | A missing/failed bit read is **never** presented as false; do not claim RCB is free if positive in-use evidence exists |
| Report activation | **Explicitly** enable a free, authorized test RCB and optionally request GI | Verified write/readback lifecycle; actual InformationReport count increases; `RptID`, optional DatSet/ConfRev/DataRefs and ordered included member values match live RCB | A successful GI Write is **not** proof that a report arrived; reject foreign/stale/invalid DataRef without relabeling |
| Multiple RCBs | While subscribed to RCB A, select RCB B (inspection only) | A subscription reference stays pinned; B says **OTHER RCB ACTIVE**; B inspector never displays A's frame as B; **Stop active RCB** names A | B must not be mutated by selection or A's Stop; same RptID is not sufficient to identify the publisher |
| RCB cleanup | Stop owned subscription and/or disconnect/reconnect | Only locally owned changes cleaned up, subscription inactive, cleanup status confirmed, re-acquisition succeeds if allowed | Lost association/failed cleanup shows `CLEANUP REQUIRED`; no hidden "all released" assertion |
| GOOSE | Browser → GOOSE → configured stream → explicit NIC → **GOOSE Subscribe** | Stay in Browser, actual wire APPID/goCBRef/source/destination MAC/goID/VLAN/PCP, stNum/sqNum/TAL/packet counts, ordered allData | Unknown/ambiguous publisher fails closed; do not map member names until ConfRev **and member count** agree; VLAN/goID-distinct same-source publisher must not overwrite another |
| GOOSE Stop | Stop passive capture | Retained last frame labeled historical, not live; PCAP export contains actual observed bytes | No GOOSE pub/sub configuration Write, no automatic NIC pick, no fabricated subscription acknowledgement |
| Setting Groups / Files | Read SGCB and list server files without altering relay | Effective SGCB state, selected group and safe server file listing; data source obvious | SG change/file mutation only with explicit user confirmation and verified readback or guarded failure |
| Multi-IED / responsiveness | Select 2 independent endpoint slots and a 60k-node large fixture | IED-specific selection, no cross-source value leak, scrolling and filtering remain responsive | No eager full tree rewalk per signal, hidden per-row MMS polling, duplicate parser or unbounded report/GOOSE history |

## 4. Reference-style visual checkpoints

For each of **Data Model, DataSets, Reports, GOOSE, Setting Groups, Files, Global Data**, capture the same state and screen dimensions as the reference ARSAS/ARIEC application where that view actually exists. Compare in a recorded two-column checklist: navigation level, information density, primary operator affordance, bit/options alignment, row striping, truncated/full tooltip details, warnings, and input/disabled states. An approximate Qt layout is **not** a claim of pixel-exact screenshot identity. Change GUI presentation only where underlying source authority remains valid.

## 5. Evidence package and verdicts

Save a timestamped folder (redact production IP/usernames/secrets): `workbench_build_sha.txt`, `hardware_firmware.txt`, `actions_release_run.txt`, `network_nic_vlan.txt`, `mms_report_trace.pcapng` or vendor-safe MMS trace, `goose_observed.pcap`, and paired `reference-vs-workbench` screenshots for each selected surface. Capture RCB readback **before / during / after** activation with exact reference, received/rejected/decode-failed counts, SqNum, EntryID, ConfRev, DataRef and critical diagnostic text.

Verdict each matrix row **PASS / FAIL / BLOCKED / NOT RUN**. Mark `BLOCKED` with why (e.g., no GOOSE publisher, multicast not mirrored, no authorized spare RCB, missing Npcap, vendor denied RCB Read). Do not silently promote `NOT RUN` to `PASS`, and do not treat simulated loopback as hardware certification.

**Release acceptance gate:** a ready-to-install package must be traced to the merged code and verify on an actual Windows PC. **Physical IED acceptance gate:** requires actual on-wire relay behavior and operator screenshots. **Reference GUI pixel parity gate:** requires direct comparison against the specific reference screens at identical state/size. All three are separate decisions.
