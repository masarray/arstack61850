# ARStack Process Bus Simulator M3.H.2 RC1 — Public Engineering Preview

**25 October 2026 — prerelease, NOT Public Stable.** This release candidate is intended for isolated, non-production IEC 61850 SV/PTP laboratory experiments. The latest tested stable M3.H.1 continues to be [processbus-sim-m3h1-20261024](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h1-20261024), without any changed assets or firmware.

## What's improved

- **Current-session SV STOP alarm:** board-retained `SVLIFE stop_cause=RUNTIME_FAULT` history no longer generates a frightening error on ordinary Studio startup. A red unexpected-STOP alarm requires an observed transition from verified RUNNING to STOPPED during the current session. New fault evidence remains diagnosable.
- **Compact hardware status LED:** DISCONNECTED (grey), CONNECTING (blue), PREPARING (amber), READY/RUNNING (green) and ERROR (red), sourced from the active serial/identity/supervisor state.
- **Canonical SV TX diagnostics:** first/last ESP-IDF Ethernet transmit errors, link evidence and contiguous-burst/stop history are retained across operator restart. The fail-closed **400 consecutive TX failure** threshold has **not** been bypassed.
- Exact Windows GUI and ESP32-P4 firmware pairing is verified in the source-head CI and installer lifecycle tests.

## Exactly which binaries

| Evidence | Immutable source |
| --- | --- |
| Studio + paired firmware source | PR [#328](https://github.com/masarray/arstack61850/pull/328) reviewed head `fb81c247fb24cd6b5ac6aad6b3fb27486d620b96` |
| Windows setup + portable build | [GitHub Actions run 40145602361](https://github.com/masarray/arstack61850/actions/runs/40145602361), artifact `arstack-studio-native-preview-win-x64-328-fb81c247fb24cd6b5ac6aad6b3fb27486d620b96` |
| Firmware built from same head | [Run 40145602361](https://github.com/masarray/arstack61850/actions/runs/40145602361), firmware `sourceCommit=fb81c247fb24cd6b5ac6aad6b3fb27486d620b96` |
| PR CI | 10 success, 1 conditional skip, 0 failures; Windows native, Qt headless/HiDPI, firmware, cleanup and installer migration |
| Merge to main | `c8d351f1fe80ca4cf8f3e58cf6727566bec7d1f6` (packaged runtime is PR head above; subsequent changes are release paperwork) |

Download the Windows **Setup** or **Portable ZIP**, confirm the included `SHA256SUMS.txt`, and use the **firmware bundled with that package**. Do not mix preview and old stable firmware. The executable still reports the internal `0.1.1-preview` engineering version; **M3.H.2 RC1 is the package/release-candidate label, not a claim of a new stable semantic version or hardware-certified product**.

The separate Linux pull-request AppImage artifact was a GUI/test build and did not include a cross-platform release-tag-matched firmware package. **It is not promoted as a firmware-paired Linux release in RC1.** Public Stable M3.H.1 remains the Linux choice.

## Actual user-observed result

Two operator screenshots on COM15 show: connected idle **READY**, active **RUNNING**, and a displayed snapshot of approximately **3,999 fps, MISSED 0, TX FAIL 0**. These prove useful operator-state UX and that a board claims RUNNING at that moment. They **do not** establish a 1-hour endurance pass, independent NIC capture, reception by DigSubAnalyzer/ARSAS, authenticated PTP grandmaster synchronization, or error-free long-term Ethernet transmission.

## Stable promotion — blocked until these are completed

- [ ] Physical **3600 s** retained run on this exact candidate, complete start/stop cycling and no missed frames/transport faults; attach test conditions and trustworthy raw logs.
- [ ] Recipient NIC unfiltered PCAPNG captures actual 0x88BA packets, VLAN/tag state, APPID, svID and sample-counter continuity; note NIC/offload/switch topology.
- [ ] **DigSubAnalyzer** receives and decodes the live stream with values and sample counter.
- [ ] **ARSAS** receives and decodes the same real stream independently, with filters/NIC identity recorded.
- [ ] Verify full package SHA-256, firmware manifest/source pairing, installer migration and Windows/Linux cross-platform distribution for the new stable release tag. Do not relabel these preview binaries stable.
- [ ] Activate and independently verify [main branch protection](https://github.com/masarray/arstack61850/issues/322) under authorized repository administration before calling governance complete.

Field incident: [#324](https://github.com/masarray/arstack61850/issues/324). Release gate/runbook: [independent SV wire capture](../P0_SMV_WIRE_CAPTURE_ACCEPTANCE.md) and [RC2 physical acceptance](../SMV_STUDIO_RC2_ACCEPTANCE.md).

This candidate is strictly **prerelease=true**. The workflow refuses to overwrite or downgrade the existing Public Stable release, and its artifacts are pinned and checksum-checked before publication.
