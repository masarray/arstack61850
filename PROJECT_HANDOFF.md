# ARStack61850 Project Handoff

> **Latest public simulator milestone — 2026-10-24:** [ARStack Process Bus Simulator M3.H Lab Edition](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h-20261024) is PUBLIC STABLE as an engineering lab simulator. The verified release firmware/Studio source is `a922e9bdba4e57a4e88406eb83344455dd8ff142` (10/10 CI PASS), with `main` carrying documentation synchronization rather than unreviewed integration of the large 636-commit active implementation chain. For source work use [PR #298](https://github.com/masarray/arstack61850/pull/298) (Draft) and the [public M3.H evidence/quick start](docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md). Real PCAP: 163,048 SV over 40.763 s, all `smpSynch=2`, zero missing/duplicate/malformed; 44 Announce + 43 each Sync/Follow_Up. Capture truncated by SV STOP before requested 120 s. **No relay/GPS is needed to run the lab simulator; no physical relay lock test was performed.**
>
> **M3.H release vs source integration:** Public Stable means the verified engineering lab simulator **binary** is released, not that Draft PRs #295–#298 or the 600+ commit source stack are merged into `main`. The firmware baseline remains `a922e9bdba4e57a4e88406eb83344455dd8ff142`. Earlier v0.1.0 `smpSynch=0` is historical—not M3.H behavior (100% `smpSynch=2` in the valid 40.76-second capture). A physical relay lock test, GPS traceability, and 120-second endurance are not claimed.

> **Do not confuse product tracks:** the Workbench/Browser SSOT handoff below and historical SMV `v0.1.0` results are separate from the latest published M3.H SV + simulated PTP Grandmaster instrument. The realtime M3.H firmware remains pinned; do not merge all experimental branches just to update web pages.

> **Native Workbench P0, 2026-10-17:** [Whole-application SSOT Live Data Model master handoff](docs/WORKBENCH_SSOT_LIVE_DATA_MODEL_MASTER_HANDOFF.md) is the up-to-date mandatory entry point. It records unresolved canonical data convergence, PR #230's failing exact-head gate, ownership, and a concrete implementation/acceptance sequence. Older Browser GUI progress below is **not** proof of completed Workbench SSOT or field UX acceptance.

This file is the **current-state handoff** for a new engineer or AI thread. Read [`NORTH_STAR.md`](NORTH_STAR.md) first for the long-term direction, then use this file to continue active development without reconstructing the bench history from chat.

> **Current native IEC 61850 Workbench / Browser engineering track (M2A–M2J merged; M2K PR #176 active as of 2026-10-15):** read [docs/WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md](docs/WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md) for the shared ARSAS-inspired GUI/SSOT execution plan, cross-thread ownership and CI/merge gates; then read [docs/WORKBENCH_CURRENT_HANDOFF.md](docs/WORKBENCH_CURRENT_HANDOFF.md) for the latest checkpoint and active blockers. **Always refresh GitHub refs**. The SMV/ESP32-P4 release history below is a separate product track.

Last public simulator milestone: **2026-10-24 — Public Stable Process Bus M3.H released**; earlier **2026-09-15 — stable SMV-only v0.1.0** remains historical.

## 1. Active repository work

- Repository: `masarray/arstack61850`
- Production branch: `main`
- SMV production merge: `9c7fc7300220db4643e5643081240b955cfe12df` — PR #79
- Latest Public Stable lab SV/PTP simulator: [`processbus-sim-m3h-20261024`](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h-20261024); tested source `a922e9bd...` on active Draft PR #298
- Historical stable SMV-only release: [`v0.1.0`](https://github.com/masarray/arstack61850/releases/tag/v0.1.0)
- P0.5 supervisor/physical release issue: #83 — closed
- Legacy first-trial SV PR #15 — archived/superseded, not merged
- IED Simulator PRs #75/#82 remain separate tracks and must not be conflated with the SMV release baseline

For the current SV/PTP *simulator* release, start with [`docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md`](docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md); for the older v0.1.0 SV-only record use [`docs/SMV_STUDIO_V0.1.0_PUBLIC_RELEASE.md`](docs/SMV_STUDIO_V0.1.0_PUBLIC_RELEASE.md). Sections below retain useful historical bench/toolchain context; where they conflict with the versioned release record, the release record and current `main` take precedence.
## 2. Tested hardware/toolchain

### Device

- ESP32-P4 Ethernet development board
- ESP32-P4 detected silicon revision: **v1.3**
- onboard RMII PHY
- physical interface: RMII
- board flash physically detected as 32 MB, while the current development image header is still configured smaller; this is not blocking current P2 work but should be normalized later.

### Toolchain

- tested: **ESP-IDF v5.5.5**
- early ESP32-P4 silicon requires the pre-v3 revision configuration; do not flash a v3.1-minimum image with `--force`.
- local `sdkconfig` on the tested machine contains the early-silicon setting and should not be casually deleted.

### Host capture bench

- USB 2.0 Fast Ethernet capture adapter
- host capture successfully receives broadcast diagnostic traffic.
- this host adapter/path has not reliably exposed the canonical multicast + VLAN process-bus stream and introduces substantial arrival-timestamp excursions/batching at ~4000 fps.
- therefore **host USB-Ethernet capture timestamps are not device timing truth**.

## 3. Proven ESP32-P4 bring-up

### P0 — physical/data-plane path: PASS for current bench

Observed on real hardware:

- ESP32-P4 boots firmware;
- onboard RMII PHY initializes;
- Ethernet link reaches `UP`;
- raw frames emitted by ESP reach host capture;
- diagnostic EtherType `0x88B5` probes are visible in packet capture;
- IEC 61850 `0x88BA` frames are visible through the diagnostic broadcast path.

### P1 — IEC 61850 SV encode/decode: PASS for bench foundation

Observed:

- emitted frames are independently recognized as IEC 61850 Sampled Values;
- a raw process-bus analyzer decodes `svID`, APPID, `confRev`, `smpCnt` and 4I+4V payload shape;
- sequence continuity works when the diagnostic stream uses its own identity/counter policy.

This is **interoperability evidence**, not a conformance certificate.

## 4. Current P2 realtime publisher

Implementation path:

`embedded/esp32p4_smv_injector/main/app_main.cpp`

Current architecture:

- 50 Hz nominal;
- 80 samples/cycle;
- 4000 publisher ticks/s;
- 250 us period;
- ESP-IDF GPTimer at 1 MHz;
- GPTimer ISR performs only a FreeRTOS task notification;
- dedicated high-priority publisher task on CPU1 blocks between timer events;
- telemetry task is separate;
- fixed/prebuilt packet templates;
- runtime patches fixed-width `smpCnt`, `confRev` and `seqData`;
- no BER re-encode / heap allocation in the hot per-sample TX path;
- balanced A/B/C sine table;
- dataset shape: `Ia, Ib, Ic, In, Ua, Ub, Uc, Un`, each value followed by quality.

A previous busy-spin scheduler caused Task WDT / `IDLE1` starvation. It was removed. **Do not reintroduce busy-spin timing as the primary cadence.**

### Latest stable ESP-side observation

Serial telemetry in the stable run showed repeated intervals approximately like:

```text
P2 timing: samples=4000 (~4000 fps)
MC ok=4000 fail=0
mirror ok=4000 fail=0
missed=0
wake_late_us[min/mean/max]=0/0/0
```

Treat `wake_late_us` as scheduler/task evidence only. It is not yet EMAC wire-time evidence.

## 5. Canonical stream vs diagnostic stream

The two streams are intentionally separate. Never merge their identities.

### Canonical standards-facing stream

Aligned to the current sample SCL stream used on the bench:

```text
destination  01:0C:CD:04:00:01
APPID        0x4000
VLAN         PCP 4 / VID 0
svID         MU01_SV1
confRev      1
nofASDU      1
rate         4000 samples/s (50 Hz x 80 samples/cycle)
dataset      Ia Ib Ic In Ua Ub Uc Un + qualities
smpSynch     0 (truthful: no measured PTP/1PPS lock yet)
```

For the supplied SCL, canonical ASDU field presence currently follows its `SmvOpts`: dataset reference and explicit sample-rate fields are omitted where configuration says those optional fields are not carried.

### Lab diagnostic stream

Purpose: work around the current host-capture limitation so signal/timing-analysis UI can be exercised while preserving canonical traffic separately.

```text
destination  FF:FF:FF:FF:FF:FF
APPID        0x4F01
svID         AR_DIAG_SV1
VLAN         none
smpRate      4000
smpMod       SmpPerSec
smpCnt       independent normal 16-bit counter
```

**The diagnostic stream is not the compliance target.** Do not modify canonical standards behavior merely to satisfy diagnostic-stream UI assumptions.

## 6. Latest analyzer observation

A raw process-bus analyzer reconstructed the diagnostic stream into:

- clear 50 Hz voltage and current waveforms;
- balanced three-phase phasors about 120 degrees apart;
- ~4000 samples/s;
- continuous sequence / no missing samples in the stable observation window.

The analyzer also reported arrival-time excursions while device-side sequence remained continuous and average throughput stayed near 4000 fps.

Interpretation: **capture/timestamp path suspect** until hardware timestamps prove otherwise. Do not tune the ESP scheduler to mimic or cancel host USB-Ethernet batching.

### Current engineering-value caveat

The publisher currently uses a bench scaling convention (current 1 mA/count, voltage 10 mV/count). A raw analyzer that treats integer counts directly as A/V will therefore display values such as roughly 1000 A when the intended bench signal is 1 A.

Do not “fix” this by corrupting the canonical payload to satisfy one raw viewer. Scaling and semantic interpretation must move into the standards/profile layer and expected-vs-observed model.

## 7. Immediate next technical priorities

### Priority A — EMAC IEEE1588 hardware timing evidence

The ESP32-P4 Ethernet MAC exposes IEEE1588v2-capable hardware time through the pinned toolchain.

Next work should:

1. enable PTP hardware timestamp capability;
2. build an ARStack hardware-clock abstraction rather than leak version-sensitive low-level APIs everywhere;
3. correlate at least:
   - `smpCnt`;
   - scheduled sample deadline;
   - publisher submit time;
   - EMAC hardware TX time where obtainable;
4. calculate min/max/mean/P95/P99 error and missed deadlines;
5. run at least 60 s at 4000 fps;
6. repeat at 4800 fps / 60 Hz;
7. store machine-readable evidence.

Do not use host USB-Ethernet timing as the pass/fail source for this gate.

### Priority B — SCL -> compiled `SvPublisherProfile`

ARStack already has SCL model/parser structures. Extend them instead of making a separate embedded XML parser.

Desired path:

```text
SCD/CID/ICD/IID
    -> host SCL/profile compiler
    -> validated versioned binary profile
    -> management/control-plane deployment
    -> ESP fixed runtime templates
```

The profile must preserve:

- destination MAC;
- APPID;
- VLAN ID/PCP;
- svID;
- confRev;
- nofASDU;
- explicit optional-field presence (`SmvOpts`);
- dataset order/type/quality layout;
- sample-rate/sample-mode basis;
- sample-counter policy;
- engineering scaling policy;
- synchronization policy.

Reject unresolved/ambiguous bindings. Do not guess.

### Priority C — PTP / IEC/IEEE 61850-9-3

Use the evidence-oriented layering already present in related ARStack code, but make device timing hardware-based:

1. PTP L2 codec/parser;
2. passive monitor;
3. Power Profile validator;
4. ESP32-P4 EMAC hardware clock;
5. peer/path-delay and offset estimation;
6. servo/discipline state;
7. explicit `Unlocked / Acquiring / Locked / Holdover` state;
8. `smpSynch` derived from measured state;
9. synchronization/timestamp evidence.

### Priority D — analyzer hardware path

After TX timestamping is understood, add receive-side instrumentation:

- promiscuous process-bus RX;
- EMAC hardware RX timestamp metadata;
- bounded capture ring;
- per-stream sequence/jitter statistics;
- trigger engine;
- packet + metadata transport to host;
- compare hardware RX timing against host-capture timing.

## 8. Longer-term hardware direction

The current single-port ESP32-P4 board is a development platform, not the final precision-instrument form.

Future precision instrument should investigate:

- two or more process-side ports;
- copper + optical options;
- passive/transparent TAP or deterministic switch/programmable-logic front-end;
- timestamping as close to ingress/egress as possible;
- PTP-disciplined oscillator/clock domain;
- PRP/HSR-aware duplicate paths;
- high-speed isolated management/control link;
- process-network isolation from the user host;
- secure boot/update and local evidence buffering.

See [`NORTH_STAR.md`](NORTH_STAR.md) and [`docs/GLOBAL_BENCHMARK.md`](docs/GLOBAL_BENCHMARK.md).

## 9. Build / flash commands for the current tested bench

Use the ESP-IDF v5.5.5 environment. Preserve the local early-silicon `sdkconfig`.

```powershell
cd C:\Git\arstack61850
git checkout feature/esp32p4-smv-injector-p0-p1
git pull --ff-only

cd embedded\esp32p4_smv_injector
idf.py reconfigure
idf.py build
idf.py -p COM3 flash monitor
```

Exit monitor with `Ctrl+]`.

If the port changes, discover the actual COM port instead of assuming COM3.

## 10. Bench safety boundary

- use an isolated lab network, direct cable, or proper TAP/SPAN path;
- do not attach development publisher traffic to an operational substation process bus;
- treat diagnostic broadcast traffic as lab-only;
- preserve truthful `smpSynch=0` until real synchronization is measured;
- do not claim strict 9-2LE/IEC 61869-9/IEC 61850 conformance until the appropriate profile and independent evidence gates pass.

## 11. New-thread checklist

Before changing code, a new thread should answer:

- What exact branch/head/PR is active?
- Is the proposed change standards-facing, diagnostic-only, or bench-only?
- What timestamp layer is being measured (scheduler, HW TX, HW RX, reference capture, host capture)?
- Does the change preserve SCL/profile truth?
- Does it introduce allocation/logging/string/XML work into the hot path?
- Does it affect canonical traffic or only the diagnostic stream?
- What evidence will prove the change worked?

If those questions cannot be answered, stop and inspect the current code/issues before editing.