# ARD M4 — Three SCL-Driven SV Publishers, One PTP Clock, One ARStack Studio

**Status:** APPROVED IMPLEMENTATION STRATEGY / NOT YET IMPLEMENTED  
**Date:** 2026-10-25  
**Target:** ARStack Studio + ESP32-P4 Process Bus Simulator M4  
**Baseline:** Official M3.H.3; preserve existing release binaries and single-publisher behavior  
**Tracking:** [M4 issue #333](https://github.com/masarray/arstack61850/issues/333)  
**Input:** Owner's *01_SV_Stream_4I+4V_(9-2LE).scd*, SHA-256 \`a4aac361dc1a857793400cf5b9d67d4d6a9d9f04ccfa143802c9bd4b7dad4a4e\`

## 0. Mission and acceptance in one minute

**Take the uploaded SCD file, show MU 1 / MU 2 / MU 3 inside ONE Studio window, set I/U/angle/quality independently for each, and publish three separate SV streams simultaneously from ONE ESP32-P4.** All three use the **same qualified board PTP clock and sample epoch**, not three independently drifting publication clocks.

M4.0 is intentionally **three 4I+4V streams**, not an open-ended generic IEC 61850 virtual IED/server framework. Each channel may have a different waveform on each MU. The Ethernet configuration is compiled from SCL; the 250-us common sampling grid is a shared system service. A PC or USB disconnect must not cause the running board to stop. A user can close and reopen Studio and have it adopt the exact three live publisher states.

**Engineer UX:** \`Open SCD -> Three MUs discovered -> Assign each MU's waveforms -> Deploy all -> START -> Observe three SV streams\`. Use a normal isolated switch, an ordinary NIC/Wireshark and optionally relay/BCU subscribers. No GPS clock, dedicated PTP checker, spare hardware MUs, or paid conformance setup is required to **use or develop** this simulation feature.

The three SV packets represent the same sample instant; Ethernet is a **serial medium**, so three packets will have measurable, bounded **egress separation**. Do not claim physically simultaneous transmission or GPS/UTC traceability.

## 1. Verified SCD input — NOT assumed values

The actual XML contains 3 independent IED elements, each with \`AccessPoint name="LD0"\`, \`LDevice inst="MU01"\`, \`LLN0\`, \`DataSet name="PhsMeas1"\` and \`SampledValueControl name="MSVCB01"\`.

| Stream / GUI page | IED name | SCL \`smvID\` / wire \`svID\` | Destination multicast MAC | APPID |
| --- | --- | --- | --- | --- |
| MU 1 | \`MU01_4I_4V_1\` | \`MU01_SV1\` | \`01:0C:CD:04:00:01\` | \`0x4000\` |
| MU 2 | \`MU01_4I_4V_2\` | \`MU01_SV2\` | \`01:0C:CD:04:00:02\` | \`0x4000\` |
| MU 3 | \`MU01_4I_4V_3\` | \`MU01_SV3\` | \`01:0C:CD:04:00:03\` | \`0x4000\` |

**For ALL three:** SCL SV \`ldInst=MU01\`, \`cbName=MSVCB01\`; \`confRev=1\`; \`VLAN-ID=0\` (802.1Q priority-tagged, **not** untagged); \`VLAN-PRIORITY=4\`; \`multicast=true\`; \`smpRate=4000\`; \`smpMod=SmpPerSec\`; \`nofASDU=1\`. \`SmvOpts\`: \`sampleSynchronized=true\`, all \`refreshTime\`, \`sampleRate\`, \`dataSet\`, \`security\`, \`synchSourceId\` **false**.

Each \`PhsMeas1\` has exactly 16 \`FCDA\` leaves in this order: \`TCTR1..4.AmpSv.instMag.i\` and \`.q\` pairs, then \`TVTR1..4.VolSv.instMag.i\` and \`.q\` pairs. Leaves resolve using \`DataTypeTemplates\` to eight **INT32** values and eight **Quality** values = **64 sample-payload bytes** for each ASDU. I/U magnitude, phase, quality and waveform engine are independent per stream.

### Two subtle SCD handling rules

1. **Repeated APPID is deliberate input.** All three SCD streams use \`0x4000\`, with *different destination MACs and svIDs*. The compiler must preserve the exact APPID instead of secretly changing to \`0x4001/0x4002\`; show a small non-blocking “APPID shared across MACs” engineering notice with an explicit optional per-stream override for users whose subscribers require unique APPIDs. The initial collision rule rejects duplicate *complete configured wire identities* (MAC + VLAN context + APPID + svID) but does **not** reject APPID alone.
2. **Repeated \`MSVCB01\`, \`MU01\`, \`PhsMeas1\` are not one stream.** The host identity key is \`IED.name / AccessPoint.name / LDevice.inst / LN0 / SampledValueControl.name\`. Resolving by only \`cbName\` or only \`DataSet.name\` merges three IEDs incorrectly. Preserve the exact SCL \`Communication/ConnectedAP/SMV\` binding for each IED.

The *file name* includes “9-2LE” but the file itself does not provide a universal runtime family tag. For **this explicitly selected 9-2LE lab workflow**, set the compiler's \`SvProfileFamily::legacy_9_2le\` through the user's selected template/profile preference; a generic arbitrary SCD must not be inferred as 9-2LE merely from a filename. Resolve the 4000-sample modulo counter through the selected validated profile rule, not the generic \`SmpPerSec\` candidate heuristic. **Do not ask the engineer to confirm the same 4000 modulus three times.**

The supplied file contains **no PTP clock/source configuration**. ARStack's board timing is chosen once in Studio; do not invent PTP XML data or claim the SCD itself locks time.

## 2. Non-negotiable architecture decisions

| ID | Decision | Why / rejected naive approach |
| --- | --- | --- |
| D01 | The PC loads/parses SCL and compiles three validated publisher profiles; firmware receives **binary profiles, never raw XML**. | XML + heap + dynamic graph traversal inside an MCU real-time path wastes RAM and creates deadlines and security risks. |
| D02 | **Exactly three** allocated \`SvStreamRuntime\` slots for M4.0, future capacity encoded explicitly. | Not three application copies or an unbounded \`std::vector\` in firmware. |
| D03 | **One PTP time authority; one phase-qualified sample grid; one GPTimer ISR; one deterministic publisher task** for the three same-rate streams. | Three timers/tasks/semaphores create sample skew, races, context switches and Ethernet contention. |
| D04 | **Separate per-stream waveform/signal banks, sine phase accumulators, packet templates, smpCnt views, statistics, generation and state**. | GUI stream switching or firmware updates must never overwrite a sibling's settings. |
| D05 | **One serialized, bounded Ethernet TX path for canonical SV**, with shared PTP coexistence; no runtime heap, locks, logs or Qt calls in the tick path. | Avoid priority inversion and memory fragmentation; still allow low-rate PTP control task. |
| D06 | An atomic **three-profile binary transaction** with hash, version, capability and full readback; old \`PROFILE V1\` remains. | Prevent half-committed SCD sets or accidentally putting MU 3 on MU 1's MAC. |
| D07 | One GUI with **MU1 / MU2 / MU3 pagination** for the SAME Table, Waveform, Phasor editors. Global board/PTP controls stay fixed. | Avoid three windows and three active QML graph renderers; no per-page USB worker. |
| D08 | \`START\` and link fault semantics preserve M3.H.3 autonomy. New stream enable/disable actions operate at a tick boundary; no implicit sibling restart. | A USB flap/GUI close is not permission to stop or reset traffic. |
| D09 | Internal PTP SOURCE uses the board's EMAC PTP time and PTP-to-sample phase authority. All three adopt **one group synchronization decision**. | Writing \`smpSynch=2\` into three unrelated software loops would be fake synchronization. |
| D10 | First ship **same sampling rate 4000 and same counter modulus 4000** for all three from the reference SCD. | Mixed 4800/4000/12800 scheduling is a separately bounded extension, not a reason to delay this concrete use case. |

## 3. Existing components to reuse (GitHub code audit)

- **SCL:** \`include/ariec61850/scl/model.hpp\`, \`src/scl/parser_part_05_01.inc\`, \`include/ariec61850/sampled_values/publisher_profile.hpp\`. The current \`SvPublisherProfileCompiler\` resolves one \`SclSampledValuesStream\` correctly; compile three records and keep IED-qualified identity. It already has 9-2LE 4I4V matching checks.
- **Binary:** \`include/ariec61850/sampled_values/compiled_device_profile.hpp\` \`CompiledSvDeviceProfile\` and \`SvDeviceProfileBinaryCodec\` **V1**. Reuse this as a typed nested stream descriptor; wrap three records in a **new V2 aggregate envelope**, rather than duplicate leaf/wire-type serializers.
- **Firmware:** \`embedded/esp32p4_smv_injector/main/runtime_profile.hpp/.cpp\` currently has **one** \`RuntimePublisherProfile\` guarded by \`g_profile_mux\`; \`compiled_profile_adapter.hpp\` maps one 16-leaf record to a 4I+4V runtime profile. Replace the singleton *at the M4 capability boundary* with a bounded immutable roster snapshot, preserving the original V1 protocol.
- **Live setpoints:** \`include/ariec61850/sampled_values/live_signal_state.hpp\` provides \`SvLiveSignalState\`, the existing safe \`SvLiveSignalBank\` with multiple pin-protected banks and \`SvFixedPointSineEngine\`; instantiate per-stream state/engine. Do not rebuild the fixed-point sine LUT or compute \`sin()\` for every channel/packet.
- **Timebase:** \`embedded/.../app_main.cpp\` owns GPTimer, \`publisher_task\`, \`smpCnt\`, fixed packet templates/patching, PTP-phase observation and sparse MAC TX timestamp evidence. \`ptp_lab_task.hpp\` exposes the role-tagged timing authority and EMAC PTP clock lease. M4 must promote the **coherent common phase-grid design** into an enabled, measured stable route for three streams; do not simply assume that an *experimental/default-off* phase-servo branch is running in an existing shipping build.
- **Firmware control:** \`profile_control.cpp\` already has \`PROFILE BINBEGIN/BINCHUNK/BINCOMMIT\`, rollback-like staging and generation/hash; \`live_control.cpp\` already manages a coherent signal bank and \`SV-LAN-AUTONOMY-V1\`.
- **Native GUI:** \`apps/arstack_studio/src/SclProfileModel.cpp\` currently exposes *one selected* profile; \`Main.qml\` currently owns *one set* of QML current/voltage \`ListModel\` instances, with \`SignalMatrix.qml\` and \`SignalPreview.qml\`. Reuse these components, adding **a three-item stream model** and stream-specific state stores. Keep \`DeviceController.cpp\` / \`DeviceIoWorker.cpp\` as the single communication authority.

**Important performance finding:** the present single-stream hot loop can transmit a **canonical SV frame plus a diagnostic mirror** each sample. Do **not** multiply that diagnostic mirror by three blindly. M4 uses three canonical packets per tick; an optional **decimated** diagnostic or one shared diagnostics feed is separate from the real-time stream. Do not sacrifice the network budget for debugging traffic.

## 4. Target component topology

\`\`\`text
    Windows/Linux ARStack Studio (ONE APPLICATION)
    Open SCD -> existing SclParser -> three qualified SCL stream rows
        -> SvPublisherProfileCompiler + selected 9-2LE rule
        -> CompiledSvDeviceProfile V1 [3]
        -> MultiSvConfigV2 envelope + CRC32 + whole-file/profile hash
        -> ONE serial DeviceIoWorker  ---------- USB/COM ----------+
                                                                    |
    StudioStreamModel(MU1, MU2, MU3)                                v
    per-stream SetpointState[3]                  ESP32-P4 control task
      MU1 [8 I/U channels, phase, quality]       multi-profile staging/validation
      MU2 [8 I/U channels, phase, quality]       one atomic roster commit
      MU3 [8 I/U channels, phase, quality]                |
    one active GUI page + preview canvases               v
                                          MultiSvRuntime[3] (fixed capacity)
                                      +---------+---------+---------+
                                      | MU 1    | MU 2    | MU 3    |
                                      | profile | profile | profile |
                                      | sine DDS| sine DDS| sine DDS|
                                      | bank    | bank    | bank    |
                                      | TX pkt  | TX pkt  | TX pkt  |
                                      | stats   | stats   | stats   |
                                      +---------+---------+---------+
                                                  ^
                       EMAC hardware PTP time ----|---- one PTP authority
                                                  |
                                GPTimer ISR (ONE 4kHz deadline grid)
                                                  |
                                High-priority SV publisher task
                                [tick k => 3 ordered SV packets]
                                                  |
                                bounded Ethernet / EMAC TX
                                                  |
                         same sample time; three identifiable SV frames
\`\`\`

### Resource ownership

| Resource | Owner | Threading / lifetime |
| --- | --- | --- |
| SCL XML tree/type resolution and diagnostics | PC SCL compiler worker | off GUI event thread; released after validated deploy |
| Persistent setpoints, dirty state and selected page | Qt \`StudioStreamModel\` | GUI thread; indexed by stable \`StreamId\`, not tab index alone |
| Serial/USB exchange, deploy, readback, telemetry | existing \`DeviceIoWorker\` | one queue/worker with request ID and timeout |
| PTP role, EMAC time lease/epoch generation | existing PTP subsystem | one authority; no per-SV cloned PTP service |
| Sample ISR and tick number | **one GPTimer** | ISR: increment tick/notify only, no packet encoding or heap |
| Three encoder contexts and TX loop | **one realtime publisher task** | fixed contexts and buffers; no runtime memory allocation |
| MU 1/2/3 waveform/phase/quality/generation | \`SvStreamRuntime[3]\` | per-stream snapshot banks; published atomically |
| Stream telemetry and debug counters | per-stream atomic or task-owned counters | sampled by low-rate control/GUI task, not formatted in ISR |

## 5. Device data model, allocations and concurrency

Host-side canonical model is **three fully independent records**, sorted in SCL order and with a stable stream key:

\`\`\`cpp
struct SclStreamKey {          // Host-only: strings permitted
    std::string ied, access_point, logical_device, control_block;
};
struct MultiSvConfigV2 {
    uint16_t schema_version = 2;
    uint8_t stream_count = 3;
    uint8_t timing_group = 0; // one PTP domain/epoch
    uint32_t transaction_id;
    uint64_t config_hash;
    std::array<CompiledSvDeviceProfile, 3> streams;
};
\`\`\`

Firmware runtime is allocation-bounded and never owns XML or host strings beyond the already bounded transmitted fields:

\`\`\`cpp
inline constexpr size_t kM4MaxSvStreams = 3;

struct SvStreamRuntime {
    uint8_t slot;
    RuntimePublisherProfile wire_profile;   // own MAC, APPID, svID, confRev
    SvLiveSignalBank signal_bank;            // own 4I+4V setpoints / quality
    SvFixedPointSineEngine signal_engine;    // own DDS accumulator
    PrebuiltSvPacket canonical;              // own headers and 64B payload area
    SvCounters counters;                     // own TX, missed, fault, generation
    StreamState state;                       // STOPPED/ARMED/RUNNING/FAULT
};

struct MultiSvRuntime {
    uint32_t roster_generation;
    uint64_t committed_hash;
    uint8_t active_count;
    std::array<SvStreamRuntime, kM4MaxSvStreams> stream;
    SharedPtpEpoch grid;                     // single owner, NOT cloned
};
\`\`\`

These are **pseudocode contracts**, not a claim that these symbols exist yet. \`PrebuiltSvPacket\`, \`SvCounters\`, \`SharedPtpEpoch\` and \`StreamState\` are small new C++ modules; **reuse** existing codecs, packet patch logic and sine-engine math. The existing four-bank reader-pinning mechanism remains valid **inside each separate bank**. Preallocate the three packet templates and their DMA-valid/driver-compatible storage at config commit or startup; check actual \`esp_eth_transmit\` ownership semantics before reusing buffers.

Use typed fixed-size arrays instead of firmware \`std::vector\`, per-stream \`new\`, string concatenation or general-purpose observer callbacks in the hot path. Cache **immutable** compiled leaf layout and frame offsets; patch only \`smpCnt\`, \`smpSynch\`, eight INT32 + Quality pairs and any actual variable fields. Avoid re-encoding ASN.1 BER/SavPDU or Ethernet headers every 250 us.

\`SvFixedPointSineEngine\` and quality calculations should use the existing fixed-point/LUT design; *each engine stores only its own phase state*, while the **same ROM LUT** is shared. One waveform generation \`generation\` per stream makes readback exact. Setpoint changes are published as one full stream-state commit and become effective at the next common tick boundary; a partial SET cannot expose 3 updated channels and 5 stale ones.

## 6. PTP synchronization and deterministic scheduling

The owner requires **three SV streams synchronized to the PTP supplied by this one ESP32-P4**, not three unrelated PTP slaves. For the reference SCD, all use exactly **4000 samples/s** and one ASDU/frame. Therefore:

- **One clock authority**: EMAC PTP timestamp/seconds in the selected board PTP policy (\`INTERNAL_GM_ONLY\` by default for an isolated lab, with existing receiver policy retained where supported).
- **One phase alignment**: use the existing PTP-to-GPTimer bridge and observer, anchored to the **same PTP second epoch**; only this grid emits tick notifications. The source-mode EMAC PTP clock and the SV grid must have a coherent, measured phase relation. Promote the required code path out of optional experimental build status **only after CI/bench verification**.
- **One sample epoch ID**: \`(ptp_clock_generation, ptp_second, sample_index)\`. For this SCD, 250-us nominal intervals; the same \`sample_index\` and \`smpCnt = sample_index % 4000\` apply to MU1/MU2/MU3 for a given tick.
- **Independent stream data**: MU1, MU2, MU3 produce different I/U/phase waveforms but all are evaluated **at the same sample time**. A configurable stream phase angle means a waveform **electrical** offset, *not* clock drift.
- **Late enabled stream**: if MU3 starts while MU1 and MU2 already run, MU3 first emits at the next qualified **global sample slot** with that slot's \`smpCnt\`. Do not reset the common grid or other streams to counter zero.
- **One \`smpSynch\` decision per qualified time domain**. The three outputs must never independently claim different clock statuses from stale UI data. While source/timing is unqualified, follow the existing AUTO fail-closed semantics (or explicit operator lab mode) consistently for the group. The value written to SV is an **output of clock qualification**, never the mechanism that locks it.
- If PTP role/source changes, epoch generation changes, MAC clock steps, or the phase observer detects discontinuity: revoke the synchronized claim **for the group**, track the reason, and requalify coherently. Never silently jump \`smpCnt\` or keep stale group lock indicators.
- The physical Ethernet MAC sends MU1, MU2, MU3 sequentially. Measure actual timestamp/egress separation on source TX where supported, report \`min/max slot lateness\` and \`max inter-stream egress separation\`. Scheduling sync means the **same sample epoch**; egress is predictably staggered.

### Tick execution (single realtime task)

\`\`\`cpp
void on_sample_alarm_isr() noexcept {
    // Bookkeeping + a task notification ONLY.
    ++shared_tick;            // bounded atomic/ISR-owned
    notify_publisher_from_isr();
}

void publisher_task() noexcept {
    while (true) {
        const TickBatch batch = wait_for_next_qualified_tick();
        if (!batch.available) continue;

        const RosterSnapshot roster = active_roster_snapshot(); // immutable generation
        const ClockDecision sync = shared_ptp_clock_decision(batch.epoch);
        for (uint8_t slot = 0; slot < roster.count; ++slot) {
            auto& stream = roster.streams[slot];
            if (!stream.running) continue;
            const auto state = stream.signal_bank.snapshot();
            auto values = stream.signal_engine.sample_at(batch.sample_time, state);
            // Keep timing references and payload separate from each stream's state.
            patch_prebuilt_sv(stream.packet, batch.sample_index % 4000,
                              sync.smp_synch, values, state.quality);
            bounded_eth_tx(stream.packet);
            stream.counters.record_tx_result();
        }
        record_batch_budget_and_latency();
    }
}
\`\`\`

This is behavior-level pseudocode: the actual \`SvFixedPointSineEngine\` exposes \`sample(...)\` and \`skip_samples(...)\`, so implement the common-timebase interface with **phase-continuous sample-index advance** rather than forcing a new trigonometric call or per-stream timer. If a deadline is missed, follow the existing \`missed slot\` and skip-samples accounting: **do not catch up by sending old samples in a burst**. Never lock a mutex in ISR and never log each frame.

**TX-order policy:** fixed deterministic MU1→MU2→MU3 during M4.0 (stable per-stream egress offset). Do not rotate order every tick unless measured output jitter justifies it. PTP timestamp-capable frames retain the existing role-specific TX path; coordinate EMAC/driver usage with a bounded lightweight TX arbiter or documented driver mutex, and reserve timing budget for PTP Sync/Follow_Up. Do not create a second Ethernet MAC owner or let a blocking debug send delay the canonical batch.

## 7. Throughput, memory and CPU budget

SCD frame requirement: \`3 * 4000 = 12,000\` **canonical SV Ethernet frames/s**, plus low-rate PTP. One group tick every **250 us**, up to **3 canonical frames/tick**.

**Wire-rate sanity check (illustrative, not a measured packet length):** if one transmitted SV frame occupies ~160 Ethernet bytes plus 20 bytes of preamble/IFG overhead, aggregate = \`12,000*(160+20)*8 = 17.28 Mb/s\` on 100 Mb/s Ethernet, before diagnostics and other traffic. Determine the **actual encoded frame size** from \`build_packet_template\`, and compute the real target from emitted bytes. The likely bottleneck is driver TX latency / descriptor exhaustion / CPU contention **during one 250-us deadline**, not only raw link throughput.

Acceptance budgets (proposed initial engineering targets; tune from bench data):
- \`kM4MaxSvStreams=3\`, no extra canonical publishers; reject a fourth at compile/deploy with \`CAPACITY_EXCEEDED\`.
- Record worst-case time for three packet patches + all three \`esp_eth_transmit\` calls; all **three** must finish within one 250-us interval with explicit margin and no accumulating tick backlog.
- Prefer **<150 us of measured 250-us period consumed by the SV batch** including worst observed PTP coexistence; hard fail/diagnostic if the 250-us deadline is exceeded. This is a **design budget**, not a currently measured guarantee.
- Collect \`tx_duration_us\`, \`batch_lateness_us\`, \`max_ptp_epoch_phase_error_us\`, \`missed_slots\`, \`dma_tx_busy\` and \`stream_tx_failures\`; do not put formatted logs in the data path.
- Disable the old 4-kHz diagnostic mirror by default for each extra MU. Provide at most one explicitly enabled **decimated** diagnostic path (or rely on the canonical NIC capture). Beware: duplicating all three would inflate from **12k to 24k frames/s** before PTP.
- Firmware RAM costs are **three copies of bounded profiles, signal banks, prebuilt packets and small stats**, not three RTOS stacks/timers. Quantify real IRAM/DRAM use in the firmware map file, assert a minimum free internal DMA-capable heap and headroom before accepting roster commit.
- Create packet templates once; for shared payload shape, share **immutable layout tables** and fixed-point sine LUT. Do not share mutable payload bytes, counters, flags or DDS accumulators.
- If the board is overloaded: report \`SV_GROUP_OVERRUN\` and the affected stream IDs; do not defer stale samples and silently destroy PTP correlation.

## 8. Transaction and protocol compatibility

**Keep existing M3.H.3 \`PROFILE V1\` commands untouched.** New firmware advertises \`SV-MULTI3-V1\`, \`SV-SCL-ROSTER-V2\` and \`PTP-SHARED-GRID-V1\` only after implementation. Studio chooses the V2 route only when firmware identity/capability explicitly matches.

Proposed bounded USB control protocol:

\`\`\`text
MULTISV BEGIN <txn> <count=3> <bytes> <hash> <schema=2>
MULTISV CHUNK <txn> <offset> <hexbytes>
MULTISV VALIDATE <txn>
MULTISV COMMIT <txn>
MULTISV STATUS                     # committed generation/hash/capabilities
MULTISV SHOW                       # read back all three immutable profiles
SVSTREAM <id> SHOW                 # actual running state, counters, setpoint generation
SVSTREAM <id> SETPOINTS <atomic-encoded-bank>
SVSTREAM <id> START|STOP           # optional per-stream controls
SVSTREAM ALL START|STOP            # operator actions, one coherent group epoch
\`\`\`

**Wire envelope V2:** magic, schema version, supported stream count, common timebase/rate group, aggregate length, transaction ID, monotonic generation, canonical digest/CRC, and exactly three embedded, size-bounded \`CompiledSvDeviceProfile V1\` records. CRC rejects accidental corruption; compare the strongest existing end-to-end binary identity hash/digest for readback. Do not serialize C++ object memory directly (padding, endianness, ABI instability).

**Two-phase commit:** \`BEGIN/CHUNK\` builds an inactive staging buffer; \`VALIDATE\` checks complete record, three supported layouts, unique full wire identities, VLAN, noASDU, counter policy, 4000 group sampling, aggregate TX budget and resource availability. \`COMMIT\` swaps **one roster pointer/generation** at a safe tick boundary (or while all stopped in initial M4.0); a failed commit leaves the previous roster untouched. Streams that remain running may be updated only via small **per-stream atomic setpoint snapshots**, not by changing network identity or payload layout.

**Recovery:** serial interruption during staging ⇒ abort or resumable bounded transfer identified by txn+hash, never partial commit. Reconnect reads board identity/boot ID, roster hash and all stream states before presenting an authoritative UI. Studio caches drafts **per stream key** and reconciles separately; does not replay START or overwrite live states.

**M4.0 safety simplification:** immutable SCL roster changes require **ALL SV streams STOPPED**; per-stream waveform/quality changes are live. This keeps rollback and sample-epoch ownership simple. Controlled hot-replacement of immutable stream identities is a later enhancement, not the first-release task.

## 9. Qt/QML GUI design — one window, three coherent pages

Retain \`apps/arstack_studio/qml/Main.qml\` visual design, \`SignalMatrix.qml\`, \`SignalPreview.qml\`, waveform/phasor docks, global PTP controls and connection/firmware strip.

\`\`\`text
+---------------------------------------------------------------------+
| ARStack Studio  •  Connected ESP32-P4  •  PTP SOURCE  •  SV 3/3 RUN |
+---------------------------------------------------------------------+
| SCD: 01_SV_Stream_4I+4V_(9-2LE).scd        [Import] [Deploy all] |
| [ MU 1 • MU01_SV1 ] [ MU 2 • MU01_SV2 ] [ MU 3 • MU01_SV3 ]        |
+---------------------------------------------------------------------+
| STREAM IDENTITY: MU01_4I_4V_2 · MAC ...:02 · APPID 0x4000 · 4kHz   |
| [ Signal Table ]  [ Waveform ]  [ Phasor ] (existing dock options)|
|  Ia Ib Ic In  / Ua Ub Uc Un  | RMS | Phase | Quality | Enable      |
|  edits affect ONLY SELECTED MU; apply bank atomically to this slot |
+---------------------------------------------------------------------+
| [Start MU] [Stop MU]             [Start all] [Stop all]            |
| MU1 4000fps ✓  MU2 4000fps ✓  MU3 4000fps ✓  • common PTP epoch   |
+---------------------------------------------------------------------+
\`\`\`

- Add \`StreamRosterModel : QAbstractListModel\` with \`streamId\`, \`iedName\`, \`svId\`, \`appId\`, \`mac\`, \`vlan\`, \`status\`, \`rate\`, \`txFrames\`, \`lastError\`, \`scenarioGeneration\`. It owns **three StreamScenario records** (8 channel values + phase angles + shape + frequency + quality + enabled).
- Add one \`selectedStreamId\` navigation property. **Table, Waveform, Phasor all bind to the selected stream scenario simultaneously**. Switching MU changes the view, **not** the underlying three snapshots. Preserve focus/partially edited values by committing editor input to that stream before switching, or storing a per-stream draft; never let QML \`ListModel\` rows for MU1 become MU2's setpoints.
- Reuse one \`SignalMatrix\` and one active \`SignalPreview\` for currently shown page. \`SignalPreview\` already computes Qt Canvas preview from a signal model. Redraw only visible canvases; cap normal preview at ~30 fps, **never at 4kHz**. Keep optional detached phasor/waveform windows bound to the same selected stream, unless user explicitly pins a view to a specific stream.
- Existing \`DeviceIoWorker\` remains the **sole serial authority**. The heavy XML/profile compilation can execute on one background worker and return immutable results via queued Qt signals. **No three USB worker threads and no three full App instances**.
- UI accepts a real local SCD as one action, auto-discovers exactly the 3 independent LLN0/SVCB candidates, shows three rows, and offers a default **9-2LE / 4000** template selection for this known sample. Unknown/unsupported data layouts appear as actionable human-language diagnostics.
- Global toolbar shows shared board power/link/PTP and 3-stream aggregate traffic; each tab shows **its own** setpoints, waveform/phasor, counters and independently acknowledged operator controls. If one stream is stopped, other pages' previews must not reset.

## 10. Practical software verification and regression plan

**A. SCD parser fixture:** keep a sanitized equivalent of the supplied user input in \`tests/fixtures/scl/multi_3mu_4i4v_9_2le.scd\` (or import the exact owner-authorized sample). Pin the sample's supplied SHA-256 in this ARD. Test three detected IEDs **in original order**, 16 leaves/64B each, 4000 samples/s, \`smvID\` triplet, precise multicast MAC triplet, priority-tagged VLAN 0/PCP4 and the **same APPID 0x4000**. A compiler that reports one merged profile or mutates APPIDs fails.

**B. Compiler/codec:** 3 repeated V1 profile encodings nested in V2 with exact round-trip/hash; unsupported 4th stream, missing \`DataTypeTemplates\`, duplicate full wire identity, invalid VLAN and wrong rate rejected. Failed final CHUNK/COMMIT must leave old roster intact. Old V1 decoder/firmware remains unaffected.

**C. Schedule simulation (no hardware needed):** simulate at least 4,000 ticks spanning a full common PTP second. Verify per tick:
\`\`\`text
MU1.smpCnt(t) == MU2.smpCnt(t) == MU3.smpCnt(t) == t % 4000
MU1.svID != MU2.svID != MU3.svID
MU1.payload <- ONLY MU1 signal generation
MU2.payload <- ONLY MU2 signal generation
MU3.payload <- ONLY MU3 signal generation
TOTAL_CANONICAL_TX_PER_SECOND == 12000
\`\`\`
Stop/restart MU2 mid-second: MU1/MU3 counters and DDS states unchanged; MU2 reattaches to the current common tick. Change MU3 Ia phase/RMS and assert MU1/MU2 packets remain byte-for-byte unchanged except allowed shared time fields. Assert PTP generation loss changes **all three** synchronization decisions coherently.

**D. Real-traffic CI/bench:** produce a paired firmware + Studio test build and verify actual Ethernet \`0x88BA\` packets via ordinary Npcap/Wireshark on an isolated NIC (or a documented packet injection harness for CI). Test \`12,000\` canonical frames/s with 3 IED identities, counter modulo 4000 and each stream's 8 values; verify \`0x88F7\` PTP source frames alongside SV. Log **measured** packet length, worst publisher tick execution, backlog/missed slots and common PTP phase/epoch status. Quantify any per-stream egress skew rather than declaring it zero.

**E. Control and GUI:** Qt harness runs “import SCD → select MU1 → edit Ia → select MU2 → edit Ub → return MU1 → values unchanged”; render desktop and mobile-size window previews; verify per-stream PTP/started state on USB reconnect with all 3 streams still physically publishing. Serial lost during V2 staging, wrong firmware capability, link down and concurrent SET while editing a second page are must-pass negative tests.

**F. No-budget engineering feedback:** a relay/BCU user may submit APPID/MAC/VLAN/svID and a small sanitized PCAP or screenshot. Useful, but *not required* to ship a functional, standards-aware simulator. No requirement for additional GPS or external PTP GM hardware as a gate.

## 11. Ordered implementation PRs and completion criteria

| PR | Change package | Gate (must pass before next PR) |
| --- | --- | --- |
| **M4-P0A** | SCD three-IED regression fixture + \`StreamRosterModel\` host compiler + 9-2LE Class A binding | 3 exact independently resolved V1 profiles, no identity mutation |
| **M4-P0B** | V2 aggregate binary envelope, bounded staging/rollback/readback, \`SV-MULTI3-V1\` capability | V1 ABI unchanged, three V1 nested profiles round-trip, interrupted commit safe |
| **M4-P0C** | Firmware \`SvStreamRuntime[3]\`, three banks/DDS states/templates, shared single GPTimer grid + common PTP phase/epoch | 12,000 canonical frames/s in deterministic test, zero cross-MU setpoint/counter bleed |
| **M4-P0D** | \`SVSTREAM\` stream-specific setpoints/start/stop/status, fault semantics, reconnect adopt | MU2 stop does not reset MU1/MU3; disconnect Studio keeps all eligible MUs running |
| **M4-P0E** | Native Studio MU pagination using current Table/Waveform/Phasor components + deployment UX | 3 persistent pages, background SCL compile, no UI/render timing regression |
| **M4-P0F** | Matched firmware/Studio build, CI, usable Wireshark capture, README/how-to and versioned test release | Three SCL-mapped MAC/svID streams with one PTP epoch; 3 × 4I4V scenarios editable |

**Definition of done (user-visible):** On one ESP32-P4, upload the supplied SCD **through Studio**; see three MU pages; enter three different injection setpoints; press **START ALL**; see three simultaneous Ethernet SV streams with **the exact SCL MAC/svID/APPID/VLAN identities**, independent payloads and aligned \`smpCnt\` to one common PTP-referenced sample grid. Closing and reopening Studio preserves the board's actual state. All builds/CI checks green. **M3.H.3 stable remains intact** until an explicitly versioned M4 package is ready.

## 12. Excluded from M4.0

No XML parser on ESP32-P4; no three FreeRTOS publisher tasks; no per-stream PTP grandmasters; no three running QML waveform renderers; no arbitrary channel count, multi-ASDU, mixed frame rates, GOOSE/MMS virtual IED server, protective relay certification or external-GPS hardware requirement. They can be separate follow-on features, while this architecture remains extensible.

**Engineering product principle:** ONE inexpensive board, THREE independent simulated Merging Units, ONE synchronized hardware clock, ONE easy Studio GUI, REAL Ethernet traffic.
