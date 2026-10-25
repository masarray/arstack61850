# ARStack Process Bus Simulator — Next Phase: Multi-SV + SCL-Driven Merging Unit

**Product direction (25 October 2026):** engineer-first, low-cost IEC 61850 process-bus traffic simulation using one ESP32-P4 and ARStack Studio. Current official baseline is **M3.H.3**, a single active 4I+4V SV publisher profile with internal PTP SOURCE traffic and autonomous operation when Studio closes.

**Primary user outcome:** Open an SCD/CID/IID/SCL file, select multiple Merging Unit SV streams, preview their engineering identities and values, then publish the supported streams **concurrently from one ESP32-P4** to an isolated test LAN. An engineer can connect a relay/BCU or software analyzer and adjust scenarios without purchasing a bank of physical MUs or a GPS clock just to get test traffic. Subscriber behavior is device/configuration-dependent: compare APPID, destination MAC, VLAN, svID, DataSet and subscriber configuration as part of normal engineering use.

## Scope and priorities

1. **P0 Multi-stream engine — one ESP32-P4, N independently configured SV streams.** Refactor the existing single active publication pipeline into an explicit bounded array of stream instances with per-stream APPID/MAC/VLAN/PCP, svID, confRev, DataSet layout, sample rate, smpCnt, ASDU assembly, quality, live I/U/phase controls and counters. Streams share the board clock/PTP domain but **never** alias sample counters or buffer ownership. Operator can START/STOP stream(s), inspect per-stream state and stop all deterministically.
2. **P1 SCL file -> MU simulator.** The host compiler reads **SCD/CID/IID/SCL** and resolves `IED`/`AccessPoint`/`LDevice`/`LN`, `SampledValueControl`, `DataSet`/`FCDA`, `DataTypeTemplates`, and `Communication/SMV` addressing. Show a list of available stream profiles with clear identity, channel layout and unsupported elements. The engineer selects streams, maps supported 4I+4V leaves and applies board-supported presets.
3. **P2 Practical simulation UX.** Multi-stream editor and overview with per-stream signal magnitude/phase/frequency, quality, enable/disable, live traffic counters, copy/duplicate stream, SCL-vs-current-deployed identity comparison, compact troubleshooting hints for VLAN/capture adapter/APPID/svID, and export/import of a reusable lab scenario.
4. **P3 Capability expansion.** Broaden supported payload shapes/leaf types, multi-ASDU and rational profile-specific sample rates **only after** supporting runtime boundaries and tests exist; never infer unsupported DataSet layouts silently. COMTRADE/scenario playback may follow after multi-SV and SCL user flow is usable.

## System architecture (reuse the existing C++ profile compiler)

```text
SCL / SCD / CID / IID (host)
  -> existing normalized IEC 61850 type + DataSet resolver
  -> ordered SampledValueControl / Communication SV profiles
  -> explicit stream-selection / Class A deployability diagnostics
  -> immutable versioned MultiSvConfig with a bounded stream count
  -> one atomic deploy transaction while streams STOPPED
  -> ESP32-P4 validated runtime config
       -> common rational sample/PTP timebase
       -> bounded Stream[0..N-1] state + counters + prebuilt packet layouts
       -> scheduled SV frames over one Ethernet driver
  -> per-stream telemetry + Studio authoritative board-state adoption
```

The existing [SV interoperability roadmap](SV_INTEROPERABILITY_ROADMAP.md) owns type resolution, SCL tolerance rules, profile families, smpCnt policies, exact wire layout and configured/observed provenance. **Do not create a second SCL parser or copy XML code into firmware.** Host performs XML/tree/type work; ESP32 realtime path receives a prevalidated immutable profile and owns deterministic dispatch.

## Hard runtime contracts

- **Bound capacity, do not guess:** determine maximum concurrent stream count by measured ESP32-P4 RAM headroom, EMAC TX ring/driver capacity and CPU/frame-rate budget. Surface `capacity_exceeded` with calculated aggregate frame load; no unsafe “unlimited N”.
- **Hot path:** one monotonic scheduler with rational time ticks, stable deterministic ordering for same-deadline streams, no dynamic XML parsing/allocation/string formatting on frame deadlines, preallocated stream buffers, explicit bounded TX failure behavior, nonblocking diagnostics.
- **Config transactions:** validate schema/version/CRC or hash, stream identities, multicast/unicast semantics, VLAN, APPID `0x4000..0x7FFF`, DataSet payload size and aggregate throughput before commit; atomic apply only while affected streams are quiescent. No half-applied stream table.
- **Independence:** each stream has its own `smpCnt`, confRev and state. Waveform amplitude and phase edits must not unexpectedly reset `smpCnt`; frequency/rate and DataSet layout updates require a controlled epoch transition.
- **PTP:** share the existing internally generated lab PTP timing reference and preserve firmware-side SV alignment/state semantics. Keep recorded timestamp/synchronization facts available for troubleshooting; do **not** make expensive external GPS hardware or formal conformance testing a prerequisite for ordinary lab simulation.
- **Autonomy:** Studio shutdown/USB control loss does not stop already-running eligible streams; reopening Studio adopts the actual board stream roster and RUNNING/STOPPED states. Physical Ethernet loss still stops traffic fail-closed; no unsolicited restart when link returns.
- **Interop usability:** show stream MAC, VLAN ID/PCP, APPID, svID, sample/packet rate and subscriber filter tips. Engineers should be able to troubleshoot a silent relay/BCU receiver by comparing the actual stream configuration, without needing special validation equipment.

## Milestones / acceptance

**M4.0 — Multi-SV core:** Two or more configured SV publishers genuinely transmit concurrently on one powered board; each has stable independent smpCnt/APPID/svID/MAC and counters. Start/Stop and Studio reconnect do not unintentionally affect the other stream. Validate via automatic decode of emitted frames and ordinary Wireshark/Npcap bench traces.

**M4.1 — SCL-to-device:** From one representative SCL/SCD with at least two supported SampledValueControl+DataSet bindings, Studio resolves two distinct stream profiles, previews the resolved fields, rejects ambiguous/unsupported layouts with actionable text, atomically deploys them, and receiver capture sees matching identities. Avoid committing confidential customer SCL.

**M4.2 — Engineer workflow:** A lab engineer can `Open SCL -> choose MU/SV streams -> map waveform -> Deploy -> Start -> see traffic/counters -> Stop` with minimal clicks, export a scenario and recover Studio session without resetting the board.

Tests: multiple APPIDs and svIDs; mixed rate and independent smpCnt wrap; timebase/phase continuity; bounded aggregate load and rejection; malformed SCL; duplicate MAC/APPID collision; VLAN-tagged vs untagged; loss/recovery; re-open Studio while two streams RUNNING. **CI and repeatable bench injection** are the default acceptance path; community-provided relay/BCU observations improve profiles but are not a blocking release dependency.

## Non-goals

No requirement to buy a GPS/PTP grandmaster, relays, BCUs or dedicated IEC 61850 analyzers. No vendor-specific packet hacks, unsupported payload claims, certification claims, or real-substation deployment. The value proposition is **simple but powerful engineering simulation with actual on-wire traffic**.

**Next implementation starting point:** audit the existing `SvPublisherProfile` compiler, binary firmware profile format, firmware scheduler/TX ownership and Studio publisher state readback; write a bounded `MultiSvConfig` ADR and an end-to-end dual-SV CI fixture before altering the real-time execution loop.
