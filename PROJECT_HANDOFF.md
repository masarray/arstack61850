# ARStack61850 Project Handoff

This file is the **current-state handoff** for a new engineer or AI thread. Read [`NORTH_STAR.md`](NORTH_STAR.md) first for the long-term direction, then use this file to continue active development without reconstructing the bench history from chat.

Last major bench update: **2026-08-10**.

## 1. Active repository work

- Repository: `masarray/arstack61850`
- Active branch: `feature/esp32p4-smv-injector-p0-p1`
- Active PR: **#19 — Add ESP32-P4-ETH SMV injector P0/P1 and P2 50 Hz realtime foundation**
- Timing evidence issue: **#21**
- Standards-first SCL/profile/PTP roadmap: **#24**

Do not assume PR #19 is merged. Check its current head and CI before branching new embedded work.

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