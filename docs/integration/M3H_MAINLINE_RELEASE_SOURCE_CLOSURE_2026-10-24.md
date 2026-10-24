# M3.H Mainline Source / GitHub Release Closure — 24 October 2026

## Decision: Release = Main (DONE)

- **Existing PUBLIC STABLE release:** [processbus-sim-m3h-20261024](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h-20261024), GitHub release ID **422544367**, `draft=false`, `prerelease=false`, latest stable.
- **Final tested and shipped firmware/Studio source:** `a922e9bdba4e57a4e88406eb83344455dd8ff142` (ESP32-P4 Build ID `a922e9bdba4e57a4`), original source-head **10/10 CI success**.
- **Release tag commit:** `a4b6e53d0dbbcababc55ba8a4ff97ade6f2a6200`, contains 5 packaging/docs-only commits after the tested firmware/Studio source.
- **Main merge:** [PR #299](https://github.com/masarray/arstack61850/pull/299) **MERGED**, original main merge commit `ad2ebd0e7696dcbc389f1241fedbec5ecf504881`.
- **PR final CI HEAD:** `c90a9798508fe5146d53cf3173b2980ded557198` passed **14/14** independent workflows: C++ CI; Studio Qt, Windows Lifecycle, Native Preview, RC2 policy; ESP32-P4 SMV and PTP Lab; PTP Core; Embedded; IED Simulator; Interoperability Server; Security; Naming; CI Routing.
- **Git DAG provenance:** tested source and official release tag are **both ancestors of main** (GitHub compares source→main and tag→main: `behind=0`). The integration created an actual two-parent merge of source with main, then a separate tree-identical ancestry merge for the release-tag-only historical docs. It retained current main documentation and did not re-enable the historical experimental publisher workflow.
- **Post-merge source parity:** compare source SHA→main changes only 16 documentation, website, historical readme, and release/CI workflow paths. **No ESP32-P4 implementation, Studio implementation, PTP/SV core implementation, portable test implementation or CMake/source runtime delta from accepted M3.H firmware baseline.** See [staging run](https://github.com/masarray/arstack61850/actions/runs/40073574930).
- **Published assets untouched:** Windows Setup SHA-256 `4902e8bf44e78fc55a139ce84a1273f0768ef6fe10000f5d0f85d75d2f6c736e`, Portable `ca01424e4102aa65e353737024ebf3258b4d2f614fa60d932c95c353ee228d8f`, ESP32-P4 BIN `61c508e9bf1357eb964d2b817b260b6a0e985446ddd68de1d45d9467e25c4607`; manifest and SHA256SUMS also unchanged. [Real HTTP download proof](https://github.com/masarray/masarray.github.io/actions/runs/40072546814).

## PR lifecycle reconciled (DONE)

| Outcome | PRs | Evidence/rationale |
| --- | --- | --- |
| Merged directly | [#299](https://github.com/masarray/arstack61850/pull/299) | 14/14 CI; GitHub merge status merged |
| Closed as ancestry integrated through #299 | [#290](https://github.com/masarray/arstack61850/pull/290), [#291](https://github.com/masarray/arstack61850/pull/291), [#292](https://github.com/masarray/arstack61850/pull/292), [#293](https://github.com/masarray/arstack61850/pull/293), [#295](https://github.com/masarray/arstack61850/pull/295), [#296](https://github.com/masarray/arstack61850/pull/296), [#297](https://github.com/masarray/arstack61850/pull/297), [#298](https://github.com/masarray/arstack61850/pull/298) | Exact PR head each is a Git ancestor of main (`behind=0`). Each PR has a closure comment. GitHub correctly displays them as closed/unmerged individually because merged via consolidated #299; this is not lost work |
| Archived as superseded, never merged | [#289](https://github.com/masarray/arstack61850/pull/289), [#294](https://github.com/masarray/arstack61850/pull/294) | Design-only alternative and untested M3.D sibling fix; both divergent from tested final. Do not import unique changes into released SV/PTP. Links and branch history retained |

**Do not delete frozen historical branches or alter the published release/tag.** A closed PR does not imply it was independently merged via its button; Git ancestry and GitHub merge metadata are explicitly distinguished.

## Divergent SMV/PTP PR cleanup — CLOSED as historical, not merged (DONE)

All nine legacy source branches were previously **OPEN/DRAFT**. They have now been **CLOSED / NOT MERGED** after exact-head review, branch-vs-main comparison and a per-PR archival comment explaining residual requirements. All original commits and discussions remain accessible. **No experimental firmware code was imported into the accepted M3.H binary or main.** This is transparent archive hygiene, not a claim that untested features work.

| Historical PR | Prior unique commits vs released source | Final disposition / canonical unresolved acceptance |
| --- | ---: | --- |
| [#284](https://github.com/masarray/arstack61850/pull/284) | 17 | Archived SOURCE-fast acquisition / AUTO_FAILOVER candidate; physical external-GM/RECEIVER and IED evidence remain #162/#154 |
| [#272](https://github.com/masarray/arstack61850/pull/272) | 7 | Archived alternative operator alerts; missing center-screen PHY/PTP feedback retained in [#300](https://github.com/masarray/arstack61850/issues/300) |
| [#266](https://github.com/masarray/arstack61850/pull/266) | 16 | Archived stale-role recovery/clipboard variant; guarded real board STOP/readback and UX retained #300/#287 |
| [#263](https://github.com/masarray/arstack61850/pull/263) | 23 | Archived earlier firmware identity/PTP START behavior; only genuinely missing current-main behavior retained #300 |
| [#259](https://github.com/masarray/arstack61850/pull/259) | 13 | Archived old USB-flash/capture recovery variant (older firmware); targeted missing identity/telemetry UX retained #300 |
| [#256](https://github.com/masarray/arstack61850/pull/256) | 2 | Archived unverified external-GM RECEIVER MAC/phase qualification; acceptance retained #162/#154 |
| [#253](https://github.com/masarray/arstack61850/pull/253) | 25 | Archived atomic live 4I+4V transaction implementation; acceptance retained [#42](https://github.com/masarray/arstack61850/issues/42) |
| [#239](https://github.com/masarray/arstack61850/pull/239) | 17 | Archived old Studio GUI/wire SSOT alternative; missing UX and partial-update concerns retained #300/#42 |
| [#166](https://github.com/masarray/arstack61850/pull/166) | 94 | Archived broad earlier M1/M2/M3 experimental tree; physical RECEIVER/PTP scope retained #162/#154 and extended timing #21 |

Comparison counts are historical (against shipped `a922e9bd...`), not a current merge suitability statement. Each branch also diverges from current `main`; do not resurrect a whole stacked patchset. Source modifications should be separately re-implemented or selectively ported from fresh main with explicit negative/positive regression tests.

**Workbench/Browser/IED Simulator historical queue reconciled (2026-10-24):** Eight distinct, diverged pre-mainline PRs [#75](https://github.com/masarray/arstack61850/pull/75), [#130](https://github.com/masarray/arstack61850/pull/130), [#137](https://github.com/masarray/arstack61850/pull/137), [#164](https://github.com/masarray/arstack61850/pull/164), [#172](https://github.com/masarray/arstack61850/pull/172), [#219](https://github.com/masarray/arstack61850/pull/219), [#228](https://github.com/masarray/arstack61850/pull/228), [#230](https://github.com/masarray/arstack61850/pull/230) were **CLOSED NOT MERGED** with per-PR archival explanations after confirming substantial divergence. Their unique features are **not claimed complete**. [Migration tracker #308](https://github.com/masarray/arstack61850/issues/308) preserves individual source SHA references, requirements and test gates, and links canonical existing Workbench/IED feature issues. Current PR count can change with new development; do not confuse these archived historical PRs with future new PRs based on `main`.

**Actual public installer acceptance added:** [PR #306](https://github.com/masarray/arstack61850/pull/306) was merged after [3/3 GitHub Actions functional release checks](https://github.com/masarray/arstack61850/actions/runs/40086762521): real Windows Setup downloaded with official script in DownloadOnly mode and SHA-256 verified; Linux installer correctly reports when the currently latest Stable release has no published AppImage; five original M3.H release asset digests remain pinned. Linux CI build success is not a Linux release download until the AppImage+checksum are actually published.

**Safety CI:** [PR #302](https://github.com/masarray/arstack61850/pull/302) merged a strictly read-only source/tag provenance guard after 3/3 PR checks PASS; its [main run](https://github.com/masarray/arstack61850/actions/runs/40079885038) is also SUCCESS. The guard verifies that the tested source and official release tag are ancestors of main, that the **immutable tag** contains the accepted runtime implementation, and that all five asset IDs/sizes/SHA-256 still match. This gate intentionally does **not** freeze future mainline source development. The former write-capable *one-time* release-promotion workflow is retired after Stable publication, so it cannot silently change latest-release metadata or re-promote M3.H when a newer release exists.

## Issues kept OPEN on purpose — different acceptance targets

- [#162](https://github.com/masarray/arstack61850/issues/162) / [#154](https://github.com/masarray/arstack61850/issues/154): external GM/relay phase interoperability, Pdelay, real IED lock and longer endurance; PCAP proves **40.762934 s** valid SV/PTP traffic, not the interrupted 120-second request or physical subscriber acceptance.
- [#21](https://github.com/masarray/arstack61850/issues/21): extended 4800 fps/60 s HW measurement, jitter percentiles and hardware run evidence remain beyond shipped 4 kHz M3.H profile.
- [#24](https://github.com/masarray/arstack61850/issues/24) / [#36](https://github.com/masarray/arstack61850/issues/36) / [#35](https://github.com/masarray/arstack61850/issues/35): broader standards/SCL and dynamic profile/fingerprint scope.
- [#42](https://github.com/masarray/arstack61850/issues/42): broader live-signal physical acceptance not assumed merely because native code is present.
- [#287](https://github.com/masarray/arstack61850/issues/287): original v0.1.1 **external** PTP FOLLOW unlock / switch offset troubleshooting, not proven solved by isolated INTERNAL_GM_ONLY M3.H simulation.
- Existing issue comments on #162, #154, #21, #24, #287 record the exact achieved/not-achieved boundaries.

**Release acceptance:** Valid 40.762934-second local capture: 163,048 4I+4V SV frames, 3999.884 fps, 100% `smpSynch=2`, no missing/duplicate/reorder/malformed in live segment, 44 PTP Announce and 43 each Sync and Follow_Up. Simulator's advertised simulated Global GM metadata is not real GPS/UTC certification. External relay/Omicron/GPS not mandatory for isolated-lab simulator distribution.

**Final source mainline status: DONE**. Subsequent application/embedded work must begin from `main` and create a new tested milestone/tag, never silently mutate the immutable M3.H release binary. Public external portfolio deployment was not independently verified, a separate website-surface follow-up rather than a firmware/mainline integration blocker.
