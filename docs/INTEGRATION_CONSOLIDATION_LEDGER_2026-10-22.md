# ARStack61850 — integration preservation ledger (2026-10-22)

> **AUDIT SNAPSHOT — not hardware release certification and not evidence that every alternative PR is merged.**
>
> Repo: [masarray/arstack61850](https://github.com/masarray/arstack61850). Working integration tip: [PR #286](https://github.com/masarray/arstack61850/pull/286) on `feat/ptp-source-policy-fsm-20261022`.
> Full commit SHA **before this ledger commit**: `fecefbd21f6a8ebb9085ec72dff05fd5a1fc3a2b`. Main: `7f6b946cab8565f37c31b460463576a0bb1e3fc5`.
> This ledger is added ONLY to existing PR #286; it is never committed to `main` until authorized acceptance.

## Verified structure (GitHub API, 2026-10-22)

- Open PRs: **64**, including **12 draft / 52 ready**; open *issues excluding PRs*: **26**.
- **27 PRs in the direct base-branch chain** ending at #286; **37 PRs outside that chain**: **21 competing/side branches** based on the same PTP–SV/Studio development lineage, and **16 independent/older tracks**.
- `main` is at `7f6b946cab8565f37c31b460463576a0bb1e3fc5`, branch API reports `protected=false`, and repository ruleset list is empty (branch-protection detail API could not be read by this app). Configure required status checks + PR approvals + no force-push in GitHub settings BEFORE any integration to main.
- GitHub compare `main...#286` at snapshot: **ahead 444 / behind 0, 124 changed files**. These numbers describe that source tree, NOT all 64 PRs.
- Direct ancestry audit of all **26 upstream branch heads** against the #286 tip: **25 branch heads are ancestors**. **#256 diverged**: 2 commits that are not in the tip's git ancestry; they are subject to explicit semantic preservation audit. Never equate a stacked PR's base label to a guarantee that its moving head is included.

## Critical #256 fork: audited semantically; historical commits not contained

Source [#256](https://github.com/masarray/arstack61850/pull/256) had two later commits beyond merge-base `7442e2aa7aeeb46370e841dcf9cbf7d236a0ebde`:
- `dec6d7165cc81aaadef460a319fafebd0a0d217c`: atomic SOURCE and RECEIVER revocation in the same failure branch, via `revoke_sv_auto_claims()`.
- `86f955ee77bf00b6f9f5677943aaf087b08da0e2`: RECEIVER lock-loss logging/diagnostics and 1 Hz observer telemetry.

Observed in tip #286 `embedded/esp32p4_smv_injector/main/app_main.cpp`: `revoke_sv_auto_claims(reason)` revokes **both** qualifications only on explicit loss branches; diagnostics check `source_local_qualified() || receiver_timeline_qualified()`; periodic observer telemetry admits both SOURCE and RECEIVER roles. Thus these two *behaviors* appear carried forward in improved form, despite non-identical commit ancestry. **Not certified as full semantic equivalence by this static inspection**: preserve both original SHAs; run regression and physical receiver lock-loss bench before marking covered.

## Immutable retention / safe integration protocol

1. **Do not merge or close anything now.** Never delete the 64 source branches, change `main`, or label side PRs as implemented based on similar titles alone. The tables below pin every head SHA and link every open issue.
2. Keep user-tested exact firmware/installer SHA separate from code review. User's current board tests remain untouched by this documentation-only branch update. Don't change #285 or release assets during the audit.
3. Use **one reviewed convergence tip per technical lane**, not 64 sequential merges and not one reckless merge of unrelated Simulator/Reporting and PTP–SV changes. First lane: Studio + ESP32-P4 PTP/SV (#286 lineage); next independently qualified lanes: Workbench/Reporting/Browser, Simulator, and release/Windows.
4. For **each of 21 side branches** below: compare `merge-base...branch HEAD` against the convergence tip; map every changed function, wire contract, regression, and UI effect as **present / equivalent with proof / missing / intentionally replaced**. A filename overlap or passing tests alone is insufficient. Bring back missing unique changes by a reviewed, isolated patch and a failing-before/passing-after regression; don't blindly cherry-pick stale QML or old firmware control.
5. For **each of 16 independent PRs**: keep untouched and independently testable until its own integration lane reaches exact-SHA green CI and acceptance. Distinct Workbench/IED Simulator features must not be silently discarded or incorporated into a PTP-only convergence patch.
6. Close old PRs only **after** the relevant feature's target commit is reachable from accepted `main`, a per-PR outcome/behavior map has been reviewed, and the PR thread links accepted tests and successor SHA. Retain source branch, PR link and head SHA for auditability. Close duplicate issues only with direct evidence to the accepted test/commit (not by title).
7. For the chosen lane, retarget only its final candidate PR to `main` **after** the audit and user hardware acceptance; inspect full diff and GitHub compare again; run every path-relevant CI and exact SHA Windows native/firmware lifecycle; use merge-commit for provenance, not automatic broad squash; do not enable auto-merge.
8. Synchronization is a safety invariant: no concurrent PTP SOURCE/RECEIVER EMAC ownership, no fake smpSynch=2, one hardware EMAC PTP timebase for SV/PTP, correct GM identity, sample-zero on proven second boundary, fail-closed STOP / lease loss. Hardware FAT must prove Pdelay, relay timing lock, subscribed currents/voltages, and 20 STOP/START cycles + extended 4kHz stream stability. Static CI does **not** qualify SIPROTEC.
9. Reassess `main` commit, open PR count, SHA changes and CI after any concurrent modification; this is a timestamped inventory, not an automatically updated truth source.

## Targeted side-branch file checks (do NOT infer semantic equivalence)

Static path lookup at #286 after snapshot (not an exhaustive code audit):

| Source PR | Unique source path or functionality | Observed in #286 tip | Preservation action |
|---|---|---|---|
| #251 | `include/ariec61850/sampled_values/live_scenario_transaction.hpp`: older token/hash scenario transaction | **File absent**; newer `live_signal_transaction.hpp` exists. The two hashing implementations have different encoding/version marker details; equivalence **unproven** | Compare staged state, wire hash, idempotent ACK, partial-error rollback, generation and tests before closing #251 |
| #249 | `docs/STUDIO_BOARD_WIRE_RELIABILITY_AUDIT_2026-10-19.md`: historical GUI→wire audit | **File absent**; archived content remains at source PR SHA | Preserve original finding and decide whether to port audit text as historical documentation, without falsely making its old baseline current |
| #275 / #274 | `assets/material/run-bolt.svg` / `bolt_run.svg` | **Files absent**; current lineage has `electric_bolt_run.svg` | Verify user-visible RUN/STOP SVG shape, size, alignment and click behavior on installed Windows exe |
| #248 | `include/ariec61850/sampled_values/live_signal_state.hpp` | **Present** | Compare concurrent-snapshot guarantees and negative tests; presence alone not proof |
| #255 | `apps/arstack_studio/qml/StudioLinkSwitch.qml` | **Present** | Verify checkbox-free 4I4V and switch behavior; presence alone not proof |
| #230 | `apps/ied_simulator/src/BrowserReportSemanticBinding.hpp` | **Absent** | Independent Workbench/Reporting lane; never claim PR #286 contains it |

**Priority:** most dangerous unresolved scope is `#251/#253/#248` atomic GUI→wire transaction and streaming safety, and `#279/#278/#277` editor/plot variants. Do a behavior+test matrix against current Studio and firmware before choosing changes. Old branches remain retained. A file not in the convergence tip is NOT necessarily an unimplemented requirement, but it cannot be marked included without independent proof.

## Convergence decision order

- **Lane A — ESP32-P4, Studio and PTP/SV:** canonical `#234→...→#286`. First inspect #256 semantic fork and all 21 sibling proposals (not assumed redundant); finish device actuator for the `AUTO_FAILOVER` policy (currently pure deterministic policy core, not wired to ESP32/Studio), and test SOURCE, RECEIVER, same-EMAC lock + real SIPROTEC.
- **Lane B — IEC 61850 Workbench quality, MMS reporting & UX:** #164/#172, #219/#228/#230. Consolidate typed `Quality`/`TimeQuality`, readable report trees and DO→DA semantic mapping with native screenshot/Qt and MMS behavior tests.
- **Lane C — IED Browser static reporting:** #127/#130/#136/#137 plus relevant issue milestones; prioritize route correctness, SCL signal catalog/dataset binding, RCB ownership and multi-IED acceptance.
- **Lane D — Simulator and historical blockers:** #75 IED Simulator, #92 START clock diagnostics, #99 Studio shutdown, #138 historical handoff, and independent earlier PTP branches #153/#157/#166 (compare safety behaviors against Lane A; don't revive stale runtime wholesale).
- **Product convergence:** these accepted lanes eventually join one protected `main` and reproducible release pipeline, with preserved test manifests and commit lineage. This is ONE verified product, not ONE unreviewable mega-PR.

## Mandatory checks before merging to main

- Confirm snapshot coverage: 64 PR heads listed, 26 issues listed, all side-head diffs have decisions; no unexplained missing unique commit.
- Exact final SHA green: `C++ CI`, `PTP Core CI`, `Embedded Profile CI`, `ESP32-P4 SMV Injector`, `ESP32-P4 PTP Lab Profile`, `Security and Evidence`, `ARStack Studio Qt`, `ARStack Studio Windows Lifecycle`, native Windows preview + Inno `.exe` hash and *matched bundled firmware*; also Workbench/Simulator-specific CI for their lanes.
- Verify the installer is an actual standalone setup `.exe` from the **same** tested SHA; a proposed workflow or run artifact ZIP is not proof that the direct setup is published.
- Physical isolated SIPROTEC 5 FAT, relay PTP lock, Pdelay exchange, smpSynch evidence, 4I4V channel values, 50/60 Hz profiles, no competing GM or uncontrolled AUTO fallback, authoritative STOP and restart.
- Human review explicitly approves the full diff against current `main`, confirms its ruleset is active, and then alone authorizes merge. Current status: **NOT approved / NOT merged / hardware unproven**.

## All 64 open pull requests (heads preserved at snapshot)

| PR | Lane / retention | Head SHA | Base branch | Subject |
|---|---|---|---|---|
| [#286](https://github.com/masarray/arstack61850/pull/286) | A: direct-chain | `fecefbd21f6a8ebb9085ec72dff05fd5a1fc3a2b` | `feat/sv-ed21-rate-metadata-20261022` | feat(PTP P1): fail-closed internal/external/auto timing source policy FSM |
| [#285](https://github.com/masarray/arstack61850/pull/285) | A: direct-chain | `88575b874f35827afa54c86f294e11a798f6b8ae` | `fix/ptp-source-fast-auto-lock-20261022` | feat(SV P0): SCL-preserved IEC Ed2.1 4I4V smpRate/smpMod wire profiles |
| [#284](https://github.com/masarray/arstack61850/pull/284) | A: direct-chain | `1846fd003c97bbf58e482efe11bd2a4a69a0eac7` | `fix/bench-gm-epoch-ms-watchdog-diagnostics-20261022` | fix(SV/PTP): fast real AUTO synchronization to own PTP GM without 10-epoch delay |
| [#283](https://github.com/masarray/arstack61850/pull/283) | A: direct-chain | `3c99e06da2fce4cbcca10cf81d365a3506c9c170` | `feat/studio-vlan-ptp-gm-sv-siprotec-bench-20261022` | fix(bench): millisecond PTP epoch seeding and resilient SV session lease with diagnostic evidence |
| [#282](https://github.com/masarray/arstack61850/pull/282) | A: direct-chain | `60165e6db88f8c502aa2a4a5f8a09aae1bf06dae` | `feat/bench-emulated-ptp-gm-sv-global-lock-20261022` | feat(relay bench): Studio VLAN controls, simulated PTP GM and locked SV gmIdentity |
| [#281](https://github.com/masarray/arstack61850/pull/281) | A: direct-chain | `ec54321c4b7022e4df784725166fce720e590522` | `fix/studio-compact-safe-deferral-editor-20261021` | fix(PTP): Power Profile VLAN0/PCP4 priority tagging for relay interoperability |
| [#280](https://github.com/masarray/arstack61850/pull/280) | A: direct-chain | `6d81ad3abb6ebdc2abf0522661202c8d9b1af8c7` | `fix/p0-studio-responsive-start-first-click-20261021` | fix(studio): quiet deferred transport readbacks, compact workspace and deliberate setpoint commits |
| [#279](https://github.com/masarray/arstack61850/pull/279) | A: SIDE/COMPARE | `bc999d3d01014f0eeb6df2b30be7071cd7070dc1` | `fix/p0-studio-responsive-start-first-click-20261021` | fix(studio): truly compact 4I+4V, equal phasor/wave area and atomic input commits |
| [#278](https://github.com/masarray/arstack61850/pull/278) | A: SIDE/COMPARE | `6c42fbdb6fe7e726088fac21f6f2a65c761fb23b` | `fix/p0-studio-responsive-start-first-click-20261021` | fix(studio): truly narrow setpoint table, balanced phasor/wave, commit-on-confirm decimal editors |
| [#277](https://github.com/masarray/arstack61850/pull/277) | A: SIDE/COMPARE | `506846834bc4110594b60e7bfd45576d36f449c5` | `fix/p0-studio-responsive-start-first-click-20261021` | fix(studio): proportional 41/59 setpoint layout, equal phasor/wave, commit-only numeric editing |
| [#276](https://github.com/masarray/arstack61850/pull/276) | A: direct-chain | `b628f2a22ad887d878f84774ce2f2bcbb661fc27` | `feat/p0-center-actionable-toasts-20261021` | fix(p0 studio): balanced responsive workbench, physical lightning RUN/STOP, first-click START intent |
| [#275](https://github.com/masarray/arstack61850/pull/275) | A: SIDE/COMPARE | `bb4b02f1fa3580e30297d9bbb4e049d3b23b5073` | `feat/p0-center-actionable-toasts-20261021` | fix(studio): proportional injection/plot workspace, verified bolt RUN/STOP, first-click START handoff |
| [#274](https://github.com/masarray/arstack61850/pull/274) | A: SIDE/COMPARE | `1efcfe2aa7ab70d75ec933176df0484c22d88b70` | `feat/p0-center-actionable-toasts-20261021` | fix(p0): responsive compact 4I4V workspace, bolt RUN/STOP, reliable first-click START |
| [#273](https://github.com/masarray/arstack61850/pull/273) | A: direct-chain | `9bbe201c955f29979086f42c449836198d91c9f4` | `fix/p0-ptp-ethernet-link-preflight-20261021` | feat(ux): central actionable toast for PTP Ethernet failures and board events, including Advanced |
| [#272](https://github.com/masarray/arstack61850/pull/272) | A: SIDE/COMPARE | `a03da5e5a1de13daa9aa30bf23ed08419b4178d4` | `fix/p0-ptp-ethernet-link-preflight-20261021` | feat(p0): center-screen actionable Studio toast for board faults, PTP Ethernet and operator attention |
| [#271](https://github.com/masarray/arstack61850/pull/271) | A: direct-chain | `e44cf25099841e22268e394209767742b9499179` | `fix/p0-sv-lease-stop-provenance-20261021` | fix(p0): gate PTP SOURCE on real Ethernet link, preserve INVALID_STATE and fail clearly in Studio |
| [#270](https://github.com/masarray/arstack61850/pull/270) | A: direct-chain | `7f30f99a1fe432281a063f1ac934043fb315f9da` | `fix/p0-warm-local-evidence-20261021` | fix(p0): ESP32-P4 periodic SV lease watchdog, latched board STOP cause, Studio GUI provenance |
| [#269](https://github.com/masarray/arstack61850/pull/269) | A: direct-chain | `d21d66e2872de4cd2423fce78efd0bb7e677d3bc` | `fix/p0-ptp-sv-lifecycle-consolidated-20261021` | fix(p0): warm AUTO SV sync, non-starving lease heartbeat and Studio lifecycle evidence |
| [#268](https://github.com/masarray/arstack61850/pull/268) | A: direct-chain | `6c97195e80c0058f675ad2bf6f4be053687453dc` | `fix/p0-ptp-copy-diagnostics-verified-start-20261021` | P0 consolidate: firmware-truth PTP lifecycle, firm SV START and native clipboard tests |
| [#267](https://github.com/masarray/arstack61850/pull/267) | A: direct-chain | `24fddc945da1cd0379d9b269955d061e00ebfc2b` | `fix/p0-ptp-profile-contract-20261020` | P0 Studio: verified PTP preflight and working Timing Copy Diagnostics |
| [#266](https://github.com/masarray/arstack61850/pull/266) | A: SIDE/COMPARE | `799b66481f77abdcbdb50a6f1cab5fe165ade006` | `fix/p0-ptp-profile-contract-20261020` | P0 physical PTP stale-role recovery + verified Time Sync clipboard |
| [#265](https://github.com/masarray/arstack61850/pull/265) | A: direct-chain | `df73fea0b68e7f4766c63d7a83be1b8b9792a297` | `fix/p0-ptp-start-firmware-provenance-20261020` | P0: enforce shared IEC 61850-9-3 PTP SOURCE profile before START; truthful toggle |
| [#264](https://github.com/masarray/arstack61850/pull/264) | A: direct-chain | `c427c23850969d95708cf5b9dffbea2b3b1bc55a` | `fix/p0-flight-recorder-race-timestamp-20261020` | P0 Studio PTP START transactional readback and visible exact firmware build |
| [#263](https://github.com/masarray/arstack61850/pull/263) | A: SIDE/COMPARE | `8d50911172d4f2c52fdf950f5c33f255e90ef351` | `fix/p0-flight-recorder-race-timestamp-20261020` | P0 Studio: exact firmware build identity and stage-aware PTP START recovery |
| [#262](https://github.com/masarray/arstack61850/pull/262) | A: direct-chain | `4ab78f90b8fa38e6ea51e431b00a825fd1a263e2` | `fix/p0-sv-sync-onboard-flight-recorder-20261020` | P0 Flight Recorder Hardening: coherent event snapshots, exact MAC causes, negligible idle overhead |
| [#261](https://github.com/masarray/arstack61850/pull/261) | A: direct-chain | `24dfa68e2d20ecbbd42bdf6535cbf3f16dc72fe7` | `fix/p0-studio-physical-flash-start-feedback-20261020` | P0: on-board SV/PTP LOCK flight recorder with exact revocation reasons in Studio |
| [#260](https://github.com/masarray/arstack61850/pull/260) | A: direct-chain | `bed893cc7ec7eea77ebe076265bac247e8123bf5` | `fix/studio-first-class-capture-ux-20261020` | Physical bench feedback: prevent silent START ambiguity during USB/firmware verification |
| [#259](https://github.com/masarray/arstack61850/pull/259) | A: SIDE/COMPARE | `a852170c2e990eb75d2b8c024942f3a494651e8e` | `fix/studio-first-class-capture-ux-20261020` | P0 Studio field fixes: delayed USB firmware verification, truthful START, capture phase evidence |
| [#258](https://github.com/masarray/arstack61850/pull/258) | A: direct-chain | `c959486a658b1861b064c3cc67e77e3798efde70` | `integration/p0-receiver-ptp-sv-and-studio-gui-20261020` | Studio-first physical test: visible Capture workspace, in-app SV/PTP evidence, bundled firmware |
| [#257](https://github.com/masarray/arstack61850/pull/257) | A: direct-chain | `e8a0bef0c609c530aaae0b97145eda89246907a6` | `fix/p0-ptp-receiver-sv-timeline-proof-20261020` | Integration candidate: P0 external-GM PTP–SV proof + P1 compact Studio injector GUI |
| [#256](https://github.com/masarray/arstack61850/pull/256) | A: direct-chain | `86f955ee77bf00b6f9f5677943aaf087b08da0e2` | `fix/p0-ptp-sv-phase-freshness-20261020` | P0 RECEIVER: actual canonical SV phase/MAC proof on external GM PTP lease |
| [#255](https://github.com/masarray/arstack61850/pull/255) | A: SIDE/COMPARE | `b3632b182bd31840da5500d367dddb98a8427c56` | `fix/p0-sv-reject-unverified-start-20261019` | P1 Studio: checkbox-free 4I+4V tables and accessible 3-phase switch |
| [#254](https://github.com/masarray/arstack61850/pull/254) | A: direct-chain | `50c6e347a465506a96798276c64b5a8649c3d518` | `fix/p0-sv-reject-unverified-start-20261019` | P0 PTP–SV: fail closed on stale 1Hz phase observer while 4kHz SV continues |
| [#253](https://github.com/masarray/arstack61850/pull/253) | A: SIDE/COMPARE | `a858967b7e35f3a65efd327b96b6da73c0688d47` | `fix/p0-sv-reject-unverified-start-20261019` | P0-B: atomic running 4I+4V live setpoints with firmware commit/readback |
| [#252](https://github.com/masarray/arstack61850/pull/252) | A: direct-chain | `b0fa8361f77facbfa8e9e752564a474e45923e4c` | `fix/studio-smart-serial-reconnect-20261019` | P0-A transactional 4I+4V START, firmware ACK/readback, safe STOP and independent SV PCAP evidence |
| [#251](https://github.com/masarray/arstack61850/pull/251) | A: SIDE/COMPARE | `fd8f2fc92ee7b533f71474d9149db69741285e54` | `fix/studio-sv-stop-cancel-pending-start-20261019` | P0 SMV: atomic 8-channel firmware scenario commit with token/hash ACK and verified START |
| [#250](https://github.com/masarray/arstack61850/pull/250) | A: SIDE/COMPARE | `eb7448b23ef37721ecc02a2907554c969fa24dcb` | `fix/studio-smart-serial-reconnect-20261019` | P0 Studio: cancel queued SV START with bounded priority STOP and block bare START |
| [#249](https://github.com/masarray/arstack61850/pull/249) | A: SIDE/COMPARE | `1f9616873f8bd6bf1199ce2f9c3958088ce7b1a4` | `fix/studio-smart-serial-reconnect-20261019` | P0 Studio: fix live GUI frequency and channel ENABLE delivery; define on-wire acceptance |
| [#248](https://github.com/masarray/arstack61850/pull/248) | A: SIDE/COMPARE | `832db687fb9cb6c15c2934c61d43057e47a67fb2` | `fix/studio-smart-serial-reconnect-20261019` | P0 SMV SSOT reliability: concurrent snapshot safety and fail-closed START |
| [#247](https://github.com/masarray/arstack61850/pull/247) | A: direct-chain | `f3e445616e9e7d8ba10e787ba4248a1a0396e557` | `fix/studio-auto-sync-10s-evidence-20261018` | P0 Studio: reliable ESP32-P4 reconnect and truthful firmware detection on startup |
| [#246](https://github.com/masarray/arstack61850/pull/246) | A: direct-chain | `7dc488c2c49bde0348dcdd649de654b2d6ba9017` | `feature/studio-matched-ptp-sv-auto-20261017` | P0 SMV: qualify SOURCE smpSynch AUTO within ~6–10s without forced metadata |
| [#241](https://github.com/masarray/arstack61850/pull/241) | A: SIDE/COMPARE | `679da6ccfd7aecabb01432a02888a9ecc5426fe3` | `feature/studio-matched-ptp-sv-auto-20261017` | P0 Studio UX: authoritative 4I+4V START, compact vertical layout and readable controls |
| [#240](https://github.com/masarray/arstack61850/pull/240) | A: SIDE/COMPARE | `c86bb64e24def5138caa234cc2eb7ee31c41d732` | `feature/studio-matched-ptp-sv-auto-20261017` | Studio P0: preserve SV/PTP milestone; exact 4I+4V START snapshot and polished operator layout |
| [#239](https://github.com/masarray/arstack61850/pull/239) | A: SIDE/COMPARE | `c35c73fe59ee45cc0d2149152c5969fb0b4a5807` | `feature/studio-matched-ptp-sv-auto-20261017` | P0 Studio operator UX: table-as-wire SSOT, visible injector title, stacked 4I+4V, centered controls |
| [#238](https://github.com/masarray/arstack61850/pull/238) | A: SIDE/COMPARE | `98fae914c5a3df6725149d5c6a53e5dfad1b2690` | `feature/studio-matched-ptp-sv-auto-20261017` | Studio P0 UX: always send visible 4I+4V on START; stacked tables and control polish |
| [#237](https://github.com/masarray/arstack61850/pull/237) | A: SIDE/COMPARE | `2529638a758e7c0c6578a26a375c826b9c689184` | `feature/studio-matched-ptp-sv-auto-20261017` | P0 Studio UI polish: exact table-to-SV START, stacked 4V/4I, readable controls |
| [#236](https://github.com/masarray/arstack61850/pull/236) | A: SIDE/COMPARE | `d02bb037790942dbe2215ae7ca10eb0c9f8ee6aa` | `feature/studio-matched-ptp-sv-auto-20261017` | P0 Studio UX: exact 4I+4V START, vertical matrices, default PTP and centered controls |
| [#234](https://github.com/masarray/arstack61850/pull/234) | A: direct-chain | `0a3e9169d51c17366ba38b8e1615400246408472` | `main` | P0: ship Studio Preview with matching PTP–SV phase-evidence firmware, not obsolete v0.1.1 |
| [#230](https://github.com/masarray/arstack61850/pull/230) | INDEPENDENT/RETAIN | `b55452b85f29b59f0dc88e3be12aa5b8b9f264f1` | `main` | P0 Workbench Reports: port ARIEC61850 fail-closed SCL semantic member binding |
| [#228](https://github.com/masarray/arstack61850/pull/228) | INDEPENDENT/RETAIN | `80bd9003b459bcd6fa09ccce02363c6fc8572108` | `main` | P0 Workbench Reports: verified GVAA DO→DA labels, q/t details and fail-closed type binding |
| [#219](https://github.com/masarray/arstack61850/pull/219) | INDEPENDENT/RETAIN | `3d8edd4449ecb9371c85a7f7f86c8905d919dbb7` | `main` | Workbench Reports UX: replace opaque MMS struct strings with bounded, expandable typed rows |
| [#172](https://github.com/masarray/arstack61850/pull/172) | INDEPENDENT/RETAIN | `3c11cd3a132516930409cac18c556ce5dad4b69c` | `main` | M2I: validate selected native Quality/TimeQuality with rendered GUI evidence |
| [#166](https://github.com/masarray/arstack61850/pull/166) | INDEPENDENT/RETAIN · DRAFT | `bc7f5c204331852b571b5c2d8e01fb90be6c0a3e` | `feature/esp32p4-ptp-sv-phase-authority-m2a-20261014` | M3C.1: bind SV sample-zero epochs to consecutive PTP seconds |
| [#164](https://github.com/masarray/arstack61850/pull/164) | INDEPENDENT/RETAIN · DRAFT | `54cc8e4793199594743b4e4cabdabd25868250f1` | `main` | M2D-1: typed IEC 61850 Quality and TimeQuality in native Workbench |
| [#157](https://github.com/masarray/arstack61850/pull/157) | INDEPENDENT/RETAIN · DRAFT | `be7ef029af8491e9cc1d332abc14137c69dbd802` | `feature/esp32p4-ptp-sv-epoch-m1-20261014` | M2A: bounded continuous PTP–SMV phase authority and guarded ESP32-P4 observer |
| [#153](https://github.com/masarray/arstack61850/pull/153) | INDEPENDENT/RETAIN · DRAFT | `ccec6c41e48dde58af2d0fcc2f427b7c34d700e7` | `main` | M1: standards-aligned PTP source defaults and fail-closed SV epoch startup |
| [#138](https://github.com/masarray/arstack61850/pull/138) | INDEPENDENT/RETAIN · DRAFT | `18fc07e89cd069de28049e768c47ab28a0095ae1` | `main` | Docs: GUI/UX recovery handoff for next thread (R1B blocker and exit gates) |
| [#137](https://github.com/masarray/arstack61850/pull/137) | INDEPENDENT/RETAIN · DRAFT | `b47ac165f4ae9a3927cb81641cefc88df3e477dc` | `feat/browser-dataset-signals-r1a` | R1B: preserve exact Static Reporting route through live report attach |
| [#136](https://github.com/masarray/arstack61850/pull/136) | INDEPENDENT/RETAIN · DRAFT | `1d3899e5266a588a2602a81c0ffe06f148f4e467` | `fix/workbench-browser-recovery-20260927` | R1A: canonical Signal Catalog → Dataset Signals → bound static RCB |
| [#130](https://github.com/masarray/arstack61850/pull/130) | INDEPENDENT/RETAIN · DRAFT | `7d7cc5e9831df761cc61231a3f686b4a2e40a83a` | `fix/workbench-browser-recovery-20260927` | R1A: canonical Signal Catalog and offline DataSet browsing in IED Browser |
| [#127](https://github.com/masarray/arstack61850/pull/127) | INDEPENDENT/RETAIN · DRAFT | `3a3ceef9149fc0dd31b27f4c9232418a68fae0e6` | `main-workbench-ux-prd` | P0: restore active Browser panel routing and enforce rendered QA |
| [#99](https://github.com/masarray/arstack61850/pull/99) | INDEPENDENT/RETAIN · DRAFT | `dbb564e396ed6e6b064b9b79ad21de110d1f342d` | `main` | P0: make ARStack Studio shutdown deterministic on Windows |
| [#92](https://github.com/masarray/arstack61850/pull/92) | INDEPENDENT/RETAIN · DRAFT | `acd81524d59bd670f1ea2805ac755f5c1f34d6d5` | `release/arstack-studio-p0-freeze` | Release blocker: eliminate ESP32-P4 START timing misses |
| [#75](https://github.com/masarray/arstack61850/pull/75) | INDEPENDENT/RETAIN · DRAFT | `3f0d97a8e99e520d27664bbc5478ff13484dc9b5` | `main` | IED Simulator P0–P4: full SCL model, live state, reporting, guarded controls, and third-party interop |

## All 26 open issues (no auto-close)

| Issue | Subject |
|---|---|
| [#226](https://github.com/masarray/arstack61850/issues/226) | P0 Workbench Reports: verified SCL/MMS DO→DA hierarchy and semantic process values |
| [#162](https://github.com/masarray/arstack61850/issues/162) | M3: unified PTP–SMV context supervisor, continuous PPS↔smpCnt, dual clock authority and bounded recovery |
| [#154](https://github.com/masarray/arstack61850/issues/154) | M2: continuously PTP-disciplined SV timeline, truthful smpSynch and IED sync-alarm acceptance |
| [#139](https://github.com/masarray/arstack61850/issues/139) | P3-H: SMV standards invariants, deterministic WirePlan and recovery orchestration |
| [#135](https://github.com/masarray/arstack61850/issues/135) | Milestone R0.1 — Compact ARStack Browser workflow and 1024px layout QA |
| [#134](https://github.com/masarray/arstack61850/issues/134) | R1B — Static DataSet to exact RCB reporting workflow |
| [#132](https://github.com/masarray/arstack61850/issues/132) | Security P0: reject malformed MMS BIT STRING before reporting bit iteration |
| [#131](https://github.com/masarray/arstack61850/issues/131) | R1A — Signal Catalog and offline canonical DataSet Browser |
| [#129](https://github.com/masarray/arstack61850/issues/129) | Milestone R1 — Open SCL and Discovery converge into verified static reporting |
| [#128](https://github.com/masarray/arstack61850/issues/128) | Milestone R0 — Restore usable IEC 61850 Browser before UX expansion |
| [#125](https://github.com/masarray/arstack61850/issues/125) | P0 regression: active IED tab renders wrong Browser panel / empty signal tree |
| [#124](https://github.com/masarray/arstack61850/issues/124) | Field acceptance: P6 multi-IED, static reporting and passive MMS capture |
| [#96](https://github.com/masarray/arstack61850/issues/96) | IEC 61850 file-service golden parity: Directory/Open/Read/Close/Delete |
| [#95](https://github.com/masarray/arstack61850/issues/95) | IEC 61850 server golden parity: canonical hierarchy, semantic defaults, indexed RCB instances |
| [#88](https://github.com/masarray/arstack61850/issues/88) | P3: wire trusted SCL-assisted connect into Workbench product path |
| [#87](https://github.com/masarray/arstack61850/issues/87) | P3: integrate trusted-SCL initial snapshot into IED Connection workspace |
| [#85](https://github.com/masarray/arstack61850/issues/85) | P2: deterministic SCL-assisted MMS connect and initial FC-root snapshot |
| [#84](https://github.com/masarray/arstack61850/issues/84) | Professional parity: readable URCB/BRCB root structures for external-client discovery |
| [#56](https://github.com/masarray/arstack61850/issues/56) | P3-B0: PTP Grandmaster Clock foundation for the SCL-driven SMV Injector |
| [#42](https://github.com/masarray/arstack61850/issues/42) | P3-A1.1: live SV signal control without realtime stream rebuilds |
| [#40](https://github.com/masarray/arstack61850/issues/40) | P3-A0.1: tolerant SCL dialect normalization and vendor-extension quarantine |
| [#36](https://github.com/masarray/arstack61850/issues/36) | P3-A1: compiled SV device profile and deterministic ESP32-P4 activation |
| [#35](https://github.com/masarray/arstack61850/issues/35) | P3-A2: observed SV wire fingerprint and configured-vs-observed resolver |
| [#33](https://github.com/masarray/arstack61850/issues/33) | P3-A: vendor-neutral SCL-driven SV interoperability profile foundation |
| [#24](https://github.com/masarray/arstack61850/issues/24) | P3: standards-first SCL-driven SV profiles and IEC/IEEE 61850-9-3 PTP synchronization |
| [#21](https://github.com/masarray/arstack61850/issues/21) | P2: deterministic ESP32-P4 Sampled Values publisher and timing evidence |

## Change-control rule

Every new integration commit must reference the originating PR(s) and one of: `INCLUDED_SHA`, `EQUIVALENT_VERIFIED`, `MISSING_NEEDS_PORT`, `REPLACED_WITH_RATIONALE`, `INDEPENDENT_DEFERRED`. Nothing is marked complete solely because its PR is old. **Never overwrite the user's physical test baseline while testing.**
