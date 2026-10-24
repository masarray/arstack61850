# ARStack61850 — START HERE: verified consolidation handoff (2026-10-22)

**Audience:** next engineer / next ChatGPT thread. **Single repository:** [masarray/arstack61850](https://github.com/masarray/arstack61850). Do NOT confuse this project with ARSAS. **This is a live engineering integration audit, not a release certificate.**

## 1. Do not destroy the current working progress

- **Latest untouched user-test baseline:** [PR #285](https://github.com/masarray/arstack61850/pull/285), branch \`feat/sv-ed21-rate-metadata-20261022\`, exact HEAD \`88575b874f35827afa54c86f294e11a798f6b8ae\`. The user was testing this code on real ESP32-P4 hardware while preservation audit was performed. Never rewrite/force-push this branch, confuse an old release with this build, or switch their currently installed firmware without matching SHA.
- **Consolidation working branch:** [PR #286](https://github.com/masarray/arstack61850/pull/286), \`feat/ptp-source-policy-fsm-20261022\` stacked on #285. It is intentionally **DRAFT**. First two code commits \`78d0368753a7f88f076526ce9b2d67723caaa1f3\` and \`fecefbd21f6a8ebb9085ec72dff05fd5a1fc3a2b\` contain a pure portable \`PtpSourcePolicyMachine\` with deterministic unit tests. **No actual ESP32 failover actuator, no Studio mode control, no external source pinning, no proven external IED factory acceptance**. Later commits on #286 are audit documentation/tooling only.
- **Main frozen for this task:** at audit start \`7f6b946cab8565f37c31b460463576a0bb1e3fc5\`. Branch API showed \`protected=false\`, no repository rulesets; the connector could not read branch protection detail (403). **No merges into main; do not assume main is protected.**
- **No original PR has been closed, merged or deleted by this audit.** The source SHAs remain linked in the preserved manifest. A GitHub branch may be moved by another actor, which is why the exact 40-character SHA matters.

## 2. Hard evidence inventory

GitHub API snapshot (2026-10-22):
- **64 open PRs**, **26 open issues** excluding PRs.
- **27 canonical stacked PRs**, oldest #234 through #286. Note **PR #256 has two source commits outside the canonical tip ancestry** despite appearing in the stacked chain.
- **21 alternative/sibling PRs**, examined for **179 changed-file occurrences** against the canonical tip's blob tree: **3 exact blob matches, 169 different blobs, 7 paths absent in target**. This is a *path/blob* test; different or missing blobs do not automatically prove lost behavior or regression.
- **16 independent PRs**, **235 changed-file occurrences** inventoried; **26 occurrences absent from the PTP tip**. They are intentionally in other lanes (Workbench, Reporting, Browser, Simulator, historical release/timing). Absence in the PTP tip is **expected and unresolved**, not license to discard the source.

**Authoritative machine-readable source snapshots** (source PR head SHAs and each touched path):
1. [All 64 PR SHA heads + 26 issues](PR_PRESERVATION_SNAPSHOT_2026-10-22.json)
2. [Side PR changed paths and source/target blob SHAs, group 1](PR_SIDE_BLOB_AUDIT_GROUP_1_2026-10-22.json)
3. [Side group 2](PR_SIDE_BLOB_AUDIT_GROUP_2_2026-10-22.json)
4. [Side group 3](PR_SIDE_BLOB_AUDIT_GROUP_3_2026-10-22.json)
5. [Independent source PR paths group 1](PR_INDEPENDENT_SOURCE_GROUP_1_2026-10-22.json)
6. [Independent group 2](PR_INDEPENDENT_SOURCE_GROUP_2_2026-10-22.json)
7. [Independent group 3](PR_INDEPENDENT_SOURCE_GROUP_3_2026-10-22.json)

[Full preservation ledger with every PR and issue](../INTEGRATION_CONSOLIDATION_LEDGER_2026-10-22.md).

**Automatic validator:** [\`tools/integration_preservation_audit.py\`](../../tools/integration_preservation_audit.py). It validates that all 37 non-chain PR sources are accounted for and can run a read-only local git ancestry/blob audit. The lightweight \`CI Routing Policy\` workflow was modified to run \`--verify-only\` when the manifest is present, including while #286 is Draft; check the workflow result at exact HEAD before claiming this gate passed.

## 3. Seven absent paths from competing PRs — keep their original PR SHA

| PR | Source path absent from tip | Safe action |
|---|---|---|
| #251 | \`include/ariec61850/sampled_values/live_scenario_transaction.hpp\` | **P0 semantic audit**, compare with current \`live_signal_transaction.hpp\`: staged complete 4I4V, generation, token, hash wire encoding, ACK, rollback, expiry, readback/START. The old class has different digest encoding; do not copy it over current implementation blindly. |
| #249 | \`docs/STUDIO_BOARD_WIRE_RELIABILITY_AUDIT_2026-10-19.md\` | Preserve original audit assumptions and old evidence as historical source. Do not pretend original baseline is the current firmware. |
| #275 | \`apps/arstack_studio/assets/material/run-bolt.svg\`, \`stop-bolt.svg\` | Compare visible icon + geometry against current \`electric_bolt_run.svg\` / \`electric_bolt_stop.svg\`. |
| #274 | \`apps/arstack_studio/assets/material/bolt_run.svg\`, \`bolt_stop.svg\` | Same: visual parity check; do not carry old duplicate icon assets unconditionally. |
| #236 | \`docs/SMV_PTP_OPERATOR_BASELINE_NO_REGRESSION.md\` | Preserve operator regression requirements and port any currently untested requirements. |

Only 3 source changed-file blobs match the current tip *exactly*: #255 \`StudioLinkSwitch.qml\`, #266 \`arstack-studio-qt.yml\`, #266 \`ClipboardHarness.cpp\`. The other **169 differences require behavioral/semantic assessment**; by themselves they do not mean 169 lost changes.

## 4. Independent unique source paths to retain

Special attention:
- **#230** \`BrowserReportSemanticBinding.hpp\`; **#228** \`BrowserVerifiedReportTypes.hpp\`; **#164** \`MmsSemanticValue.hpp\`; **#172** \`BrowserEvidenceVisualQa.hpp\`: these are source-code/reporting semantics and visual evidence on a different lane, NOT part of #286.
- **#130/#127** source Browser Signal Catalog and regression tests missing from PTP tip.
- **#75** 14 missing source paths in full IED Simulator live/server interop profile, including four CI profile workflows and runtime code; highest independent-lane preservation priority.
- **#166** PTP M3E preflight script and dedicated workflow missing; compare test behavior with current PTP receiver/phase implementation before discarding.
- **#138** old GUI/IED Scout handoff missing; retain original source link as historical document.
- Additional independent paths / exact SHAs are in the three group JSON files. **Do not merge these independent branches into the PTP branch just to make path counts drop.**

## 5. Primary product path / what exists and what is still missing

**ESP32-P4 standalone 4I4V SV + PTP SOURCE:** use one real IEEE1588 EMAC timebase for PTP and SV, real MAC timestamp + epoch-locked \`smpCnt\`, automatic measured \`smpSynch\` (no fake FORCE_2 acceptance), GM identity in SV BER, VLAN tagging, Ed2.1 50/60Hz wire metadata. The #285 baseline includes these firmware/Studio changes plus direct Inno EXE *publishing workflow logic*; a direct EXE URL and hardware lock are **NOT established** by that fact. Its previous exact-SHA C++ / Qt / ESP32 / Windows lifecycle software workflows were observed passing, but this does NOT equal real relay acceptance.

**PTP source intent:** \`INTERNAL_GM\`, \`EXTERNAL_FOLLOWER\`, guarded opt-in \`AUTO_FAILOVER\`; #286 has portable policy logic only. Must connect to ESP32 PTP mode facade/receiver, exclusive EMAC role ownership and SV pause/re-arm at measured second boundary, exact selected external clock identity, guarded failure and restoration; then wire truthful Studio control. Do not imply that AUTO_FAILOVER currently works physically.

**Hardware gate:** physical ESP32-P4 ↔ reference IED isolation, Pdelay request/response, actual relay lock status, no false GNSS traceability claim, 4I4V subscription and measurements, 50/60Hz \`smpRate/smpMod\`, loss and return of external GM, 20 START/STOP cycles, continuous 4kHz capture, stop/recovery causes, matching tested firmware EXE hash.

## 6. Exact operating procedure in the next thread

1. **Open this file first.** Read the 64/26 manifest, 3 side groups and 3 independent groups before modifying code.
2. Ask which **exact PR SHA/installer SHA** the user physically tested and record pass/fail/PCAP. Do not change the tested branch while tests are running.
3. In a checkout with the manifests, run:
   \`\`\`sh
   python3 tools/integration_preservation_audit.py --verify-only
   # For a complete local ancestry audit, fetch the source SHAs from the manifest first.
   python3 tools/integration_preservation_audit.py --scan-git --target HEAD \
     --report /tmp/arstack-preservation-report.json
   \`\`\`
   An unfetched PR head is \`NOT_FETCHED_NO_CONCLUSION\`, **not lost work**.
4. Resolve priority **#251 vs #253 transactional GUI→wire parity** first, then **#279/#278/#277** editor semantics/UI, then all remaining side PR distinct effects. For each side PR record \`INCLUDED_SHA\`, \`EQUIVALENT_VERIFIED\`, \`MISSING_NEEDS_PORT\`, or \`REPLACED_WITH_RATIONALE\` with exact regression proof. A renamed file is not evidence of feature loss.
5. Protect \`main\` via GitHub rules/branch protection and required checks. The current connector **cannot write protection rules**; no assertion of protection is authorized.
6. Converge independent lanes separately (Workbench Quality/TimeQuality + Reporting, Browser + RCB, IED Simulator), each with its own CI/hardware/test evidence.
7. **Never** merge, squash, close, force-push, or publish stable release without source audit + review + user physical acceptance. Preserve original PR URLs and SHAs after any merge for historical traceability.

**Crucial status statement to preserve across threads:** This repository is now **fully inventoried, not yet fully integrated**. Every open source PR is pinned and inspectable, while most competing PR behavior and independent lane functionality is **not yet proven equivalent or accepted**. This distinction prevents false 'zero-regression' claims.
