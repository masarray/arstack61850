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

## Separate OPEN / divergent source PRs — not shipped as M3.H

Nine remaining SMV/PTP-adjacent PRs are old divergent tracks with unique commits not in verified release HEAD. **Do not auto-merge or label them equivalent.** Unique-commit counts are from head-of-PR vs shipped `a922e9bd...` before integration; refresh before any future targeted port.

| PR | Unique branch commits (relative to shipped M3.H head) | Classification |
| --- | ---: | --- |
| [#284](https://github.com/masarray/arstack61850/pull/284) | 17 | Alternate PTP SOURCE fast acquisition / external GM AUTO_FAILOVER; external receiver/relay FAT still unverified |
| [#272](https://github.com/masarray/arstack61850/pull/272) | 7 | Alternative Studio operator fault toast / UX |
| [#266](https://github.com/masarray/arstack61850/pull/266) | 16 | Earlier PTP stale role / clipboard variant |
| [#263](https://github.com/masarray/arstack61850/pull/263) | 23 | Earlier firmware identity / PTP START UX |
| [#259](https://github.com/masarray/arstack61850/pull/259) | 13 | Earlier USB identity / packet capture UX |
| [#256](https://github.com/masarray/arstack61850/pull/256) | 2 | External-GM RECEIVER phase/MAC candidate |
| [#253](https://github.com/masarray/arstack61850/pull/253) | 25 | Earlier running 4I+4V atomic changes; targeted verification required |
| [#239](https://github.com/masarray/arstack61850/pull/239) | 17 | Earlier GUI/wire SSOT variant |
| [#166](https://github.com/masarray/arstack61850/pull/166) | 94 | Broad legacy M1/M2/M3 timing stack, not the accepted on-wire M3.H line |

**Follow-up hygiene verified on 2026-10-24:** All nine divergent SMV/PTP-adjacent PRs above are now **OPEN / DRAFT**, including the eight previously non-Draft PRs #284, #272, #266, #263, #259, #256, #253 and #239. Each of the nine PRs received a canonical reconciliation comment referencing this ledger. These branches retain unique work and are not included in the M3.H shipped firmware baseline; therefore they are deliberately not marked merged or closed without a reviewed feature-by-feature disposition. New contributions must branch from the current `main` and pass targeted gates.

The other eight open PRs concern Workbench, Browser, and IED Simulator lanes; do not close or merge them as part of SMV M3.H source consolidation.

## Issues kept OPEN on purpose — different acceptance targets

- [#162](https://github.com/masarray/arstack61850/issues/162) / [#154](https://github.com/masarray/arstack61850/issues/154): external GM/relay phase interoperability, Pdelay, real IED lock and longer endurance; PCAP proves **40.762934 s** valid SV/PTP traffic, not the interrupted 120-second request or physical subscriber acceptance.
- [#21](https://github.com/masarray/arstack61850/issues/21): extended 4800 fps/60 s HW measurement, jitter percentiles and hardware run evidence remain beyond shipped 4 kHz M3.H profile.
- [#24](https://github.com/masarray/arstack61850/issues/24) / [#36](https://github.com/masarray/arstack61850/issues/36) / [#35](https://github.com/masarray/arstack61850/issues/35): broader standards/SCL and dynamic profile/fingerprint scope.
- [#42](https://github.com/masarray/arstack61850/issues/42): broader live-signal physical acceptance not assumed merely because native code is present.
- [#287](https://github.com/masarray/arstack61850/issues/287): original v0.1.1 **external** PTP FOLLOW unlock / switch offset troubleshooting, not proven solved by isolated INTERNAL_GM_ONLY M3.H simulation.
- Existing issue comments on #162, #154, #21, #24, #287 record the exact achieved/not-achieved boundaries.

**Release acceptance:** Valid 40.762934-second local capture: 163,048 4I+4V SV frames, 3999.884 fps, 100% `smpSynch=2`, no missing/duplicate/reorder/malformed in live segment, 44 PTP Announce and 43 each Sync and Follow_Up. Simulator's advertised simulated Global GM metadata is not real GPS/UTC certification. External relay/Omicron/GPS not mandatory for isolated-lab simulator distribution.

**Final source mainline status: DONE**. Subsequent application/embedded work must begin from `main` and create a new tested milestone/tag, never silently mutate the immutable M3.H release binary. Public external portfolio deployment was not independently verified, a separate website-surface follow-up rather than a firmware/mainline integration blocker.
