# M3.H Source Integration Audit — Integrated into Main (24 October 2026)

**Release state:** PUBLIC STABLE *Lab Simulator* at [processbus-sim-m3h-20261024](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h-20261024).
**Source merge state (final):** **INTEGRATED into `main` through [PR #299](https://github.com/masarray/arstack61850/pull/299)**, merge commit `ad2ebd0e7696dcbc389f1241fedbec5ecf504881`. The verified source `a922e9bd...` and public release tag commit `a4b6e53d...` are both ancestors of main (`behind=0` for each). **14/14 exact integration HEAD `c90a979...` CI workflows SUCCESS.** The code files were checked byte-identical against firmware/Studio source baseline; all post-fork main file blobs were preserved.

## Immutable shipped baseline

- Verified firmware/Studio source: `a922e9bdba4e57a4e88406eb83344455dd8ff142`; ESP32-P4 build ID `a922e9bdba4e57a4`.
- Source-head CI: **10/10 SUCCESS**, queried through GitHub Actions `head_sha=a922e9bd...`: Product Naming Guard, Embedded Profile, PTP Core, ESP32-P4 SMV Injector, ESP32-P4 PTP Lab, Studio Qt, Security and Evidence, C++ CI, Studio Windows Lifecycle, Studio Native Preview.
- Release packaging: [run 40067685350](https://github.com/masarray/arstack61850/actions/runs/40067685350), SUCCESS. Setup SHA-256 `4902e8bf44e78fc55a139ce84a1273f0768ef6fe10000f5d0f85d75d2f6c736e`; portable SHA-256 `ca01424e4102aa65e353737024ebf3258b4d2f614fa60d932c95c353ee228d8f`; ESP32-P4 firmware SHA-256 `61c508e9bf1357eb964d2b817b260b6a0e985446ddd68de1d45d9467e25c4607`.
- Existing release id **422544367** was metadata-promoted to **latest Stable** by [promotion run 40071700074](https://github.com/masarray/arstack61850/actions/runs/40071700074) with all five assets and SHA-256 unchanged. No rebuild/re-upload.
- The independent 40.762934-second active SV capture showed 163,048 packets, `smpSynch=2` throughout, no missing/duplicate/reordered sequence, and 44 Announce / 43 Sync / 43 Follow_Up. The operator-requested 120-second test was interrupted by STOP and should not be portrayed as a 120-second endurance pass. Real relay acceptance, Pdelay peer response, and GPS/UTC traceability remain unverified.

## PR topology and separation of concerns

| PR | Branch head (verified) | Base branch | Scope | Merge status |
| --- | --- | --- | --- | --- |
| [#295](https://github.com/masarray/arstack61850/pull/295) | `a0a2a4350dadacacd73120e83e3fc3b13f42edd8` | `feat/m3d-ptp-advanced-policy-diagnostics-20261024` | simulated Global GM on boot/fallback and warm SV resume | CLOSED / HEAD integrated through #299 |
| [#296](https://github.com/masarray/arstack61850/pull/296) | `34b26147ea19bd31153ced9eafba1f019151fb4f` | `fix/m3d-preserve-fresh-bench-gm-seed-20261024` (#295) | parked 4kHz clock and PTP lease/phase authority | CLOSED / HEAD integrated through #299 |
| [#297](https://github.com/masarray/arstack61850/pull/297) | `a8ca25293311575d170d1f9ef1cd3aa388a8c002` | `fix/m3e-continuous-sv-clock-authority-20261024` (#296) | Studio USB/COM lifecycle/hysteresis, safe rediscovery | CLOSED / HEAD integrated through #299 |
| [#298](https://github.com/masarray/arstack61850/pull/298) | `a922e9bdba4e57a4e88406eb83344455dd8ff142` | `fix/m3f-usb-hotplug-session-recovery-20261024` (#297) | PTP source readiness continuity and SV sync flutter regression | CLOSED / HEAD integrated through #299 |

GitHub compare of successive PR heads: #295 → #296 is ahead by 20 commits / 8 changed files; #296 → #297 is ahead by 14 commits / 9 files; #297 → #298 is ahead by 10 commits / 7 files. This is a **stack**, not four independent patches. Review submissions on #295–#297 were COMMENTED, not approval evidence; #298 has no submitted reviews at audit time.

Historical before integration, `main...`tested head `a922e9bd...` was **DIVERGED: 636 commits ahead and 14 commits behind main, 142 compared files** (the counts are directional from `main` to the M3.H source head). These are NOT safe merge-ready delta sizes. Verify freshly before any future integration because `main` can advance.

## Integration safety decision (satisfied through the guarded integration)

**Resolved via guarded integration PR #299:** no stack-by-stack blind merging, squash or force-push was used. GitHub Actions generated a two-parent merge with the exact tested source, verified all implementation files against the baseline, preserved every main-changed blob, then recorded release-tag ancestry with no tree change. The exact final candidate passed 14/14 workflows before GitHub's ordinary merge-commit operation. Branches #290–#293 and #295–#298 were then closed as already contained in main, with individually verified Git ancestry. Divergent #289/#294/#284 were NOT silently merged.

1. Preserve tagged shipped source + manifests + five immutable assets. Stable M3.H **public lab simulator** does not require external GPS, a commercial test instrument, relay or IEC certification to be shipped.
2. In a separate integration worktree/branch based on current `main`, identify only required M3.H product source changes and their prerequisites. Reconcile the existing Workbench/Browser and other `main` work before applying deltas; do not manufacture a merge commit claiming acceptance.
3. Split review by concerns: (A) simulated GM boot/role and warm STOP/START, (B) parked sample timebase/phase authority, (C) Studio USB reconnect and serial generation gates, (D) PTP positive-TX freshness and cold-vs-running behavior. Carry portable regression tests and source guards with each slice.
4. Compare and review every changed firmware file and its config/CI guards. Test with identical source SHA across Qt/Windows/ESP-IDF/PTP Core. Check hardware timestamp pairing and 4 kHz SMV continuity. Isolate failures; no silent `smpSynch` forcing.
5. Collect real ESP32-P4 evidence for changed source when integration is attempted; external IED acceptance may remain a separately qualified interoperability milestone. Never claim the 2026-10-24 stable release contains any later integration patch unless rebuilt and republished as a *new* release.
6. Only after branch reconciliation, green CI, targeted code review and relevant hardware checks should small source PRs be marked Ready and merged in dependency order.

## Publication vs integration checkpoints

- **Publication:** GitHub Stable promotion **DONE**, source README and release notes synced, site/portfolio source wording synced.
- **Integration:** **DONE** in [#299](https://github.com/masarray/arstack61850/pull/299). The eight ancestor PRs #290–#293/#295–#298 were **CLOSED with linked ancestry evidence**; closure does not falsely mark them as separately merged through their own PR numbers. No change to source firmware baseline or published release assets.
- **Web smoke evidence:** [Live HTML and HTTP asset workflow](https://github.com/masarray/masarray.github.io/actions/workflows/m3h-public-stable-live-smoke.yml). Acceptance requires an actual successful execution; a source-link check alone is not an HTTP/deployed proof.
