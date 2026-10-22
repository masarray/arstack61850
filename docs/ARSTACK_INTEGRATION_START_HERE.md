# ARStack61850 — START HERE: safe merge and zero-progress-loss

**Checkpoint: 2026-10-22.** This is an engineering handoff, not a release note, physical FAT result, or permission to merge.

## What is safe NOW

- Working convergence PR: **[#286 — integration(DRAFT)](https://github.com/masarray/arstack61850/pull/286)**, base `main`, head branch `feat/ptp-source-policy-fsm-20261022`. Main still `7f6b946cab8565f37c31b460463576a0bb1e3fc5`, **not merged**, and reported **unprotected** with no repository rulesets by available GitHub APIs.
- The direct PR chain from #234 to #286 spans 27 source PRs. At the pre-convergence snapshot it was +444 commits from main and 124 changed files; after documents/safety patches a later source comparison showed +464 commits, behind0, 137 changed files. Counts are time-dependent; refresh compare before integration.
- Source preservation ledger covers **64 open PR exact SHAs / 26 genuine issues**, broken into 27 direct-chain, 21 divergent side branches and 16 independent tracks. Side branches are NOT automatically in #286. **Never auto-close or delete them.**
- **LAST CODE SHA TESTED:** `b6c13a5e87fae89b9258834cefae2cec8be5f1c4`. GitHub showed 11 PR workflows SUCCESS: C++ CI (Linux GCC, Linux Clang, Windows MSVC), ESP32-P4 SMV Injector, ESP32-P4 PTP Lab Profile, Embedded Profile CI, PTP Core CI, ARStack Studio Qt, IED Simulator Qt, IEC 61850 Interoperability Server CI, Security and Evidence, Product Naming Guard, CI Routing Policy. Subsequent documentation commits do not change compiled code. Exact final commit must be rechecked before merge.
- Restored safety code: `49877470e34bf2883ec60e7c8dd0b758a910c51c` preserves #251 stale bank generation check and tests; `b6c13a5e87fae89b9258834cefae2cec8be5f1c4` preserves #248 concurrent publisher/diagnostics bank protection (4 banks, per-bank readers, correct sentinel=4, stress test). #250 priority STOP implementation is present, but hardware/GUI behavior not accepted yet.

## Unresolved blockers (DO NOT MERGE MAIN)

1. **#253 not integrated:** atomic transaction of all 4I4V setpoints WHILE SV RUNNING. Original branch implemented `began_running`, `applyLiveScenario` and firmware RUNNING commit; current firmware rejects CFG BEGIN/COMMIT while running. Port deliberately with current SHA's single writer, stale-generation protection, lease, no smpCnt/phase reset, negative tests and Windows serial harness. No stale code replay.
2. **#249 GUI-to-wire regression risk:** current Main.qml `setFrequencyValue`, CT shaping and preset functions call `markScenarioDirty()` (makes `pendingApply=true`) immediately before `!pendingApply` guards, suppressing that edit's live command. Fix as part of #253 full atomic live update rather than claiming that queued partial edits are atomic. Validate GUI=wire in PCAP.
3. **PTP GM/follower and AUTO_FAILOVER:** core FSM from original PR #286 exists as deterministic policy, but no complete ESP32 actuator or Studio configuration; no claim of automatic safe role switching or selected-external-GM hardware acceptance. Prevent competing grandmasters, revoke SV on failure, re-arm with selected GM ID only at proven second boundary.
4. **Actual hardware FAT:** user has not yet reported SIPROTEC relay clock lock, Pdelay Req/Resp, whole-second smpCnt reset, genuine AUTO smpSynch, matching GM identity, accepted real 4I4V values and alarms, repeated STOP/START and sustained run. Sender-only PCAP and software CI cannot prove these.
5. **Packaging:** matched SHA ESP32 firmware with **direct Windows Smart Inno setup `.exe`**, native Windows lifecycle (install/upgrade/uninstall), hashes, no old exe or ZIP-as-primary. Native preview publishing was edited in PR #285, but latest standalone installer existence not verified; don't invent release URL.
6. **GitHub main safety:** `main` currently unprotected per branch metadata and repository rulesets list empty. Enable a minimal reviewed branch ruleset with required checks, no force pushes and explicit merge review BEFORE moving draft to ready.
7. **37 side/independent PRs:** review [per-head divergence](./INTEGRATION_SIDE_BRANCH_TRIAGE_2026-10-22.md) and [full inventory](./INTEGRATION_CONSOLIDATION_LEDGER_2026-10-22.md). GUI variants #277/#278/#279 need native screenshot/UI comparison. Workbench Quality/TimeQuality, MMS reporting, Browser, IED Simulator branches are distinct product lanes; keep each branch and acceptance results, then integrate separately.

## One clean convergence route (NO broad 64-PR merge)

- Keep #286 draft and **main untouched** until blocker resolution and user hardware evidence. The PR already targets main so an eventual PTP/SV code merge is ONE reviewed merge commit, not 27 individual merges.
- For each source PR record: `INCLUDED_SHA`, `EQUIVALENT_VERIFIED`, `MISSING_NEEDS_PORT`, `REPLACED_WITH_RATIONALE`, or `INDEPENDENT_DEFERRED`. Add links to exact replacement diff, compiler/board tests and user acceptance. Preserve original Git SHA even if closing a superseded PR after merged code is reachable from main.
- Merge sequence after approval: *Studio+ESP PTP/SV lane* → independently qualified *Workbench/Reporting/Browser lane* → *IED Simulator lane* → *stable release packaging*. Do not mix unrelated branches into a blind mega-cherry-pick.
- Check against fresh main head. After resolving blockers, trigger required full CI on the main-bound PR, inspect full diff, matched installer/firmware SHA and real relay results; have an explicit human merge authorization. Never enable auto-merge.
- Old PRs are only closed **AFTER** their feature has landed in main and evidence mapping has been checked, not to make dashboard numbers look smaller.

**Thread handoff:** read this page first. Then read the linked two ledgers. Confirm PR #286 and main exact SHAs via live GitHub, fix #253 and Main.qml live command gate, prove hardware + installer. No speculative claims, no touching main, no obsolete installer, no discarding independent features.
