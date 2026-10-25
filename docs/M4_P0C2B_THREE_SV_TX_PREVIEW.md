# M4-P0C2B — Three-SV Ethernet TX preview

**Status:** experimental opt-in firmware code under PR; not an official M4 release. `M3.H.3` official stable stays unchanged.

## Achieved by this change

One ESP32-P4 uses its **existing GPTimer publisher task, EMAC transmit path, PTP clock lease and sample-count logic** to generate **three canonical SV frames per shared 250 µs tick** instead of cloning entire FreeRTOS publishers. The source is the exact three-record SCD-derived `ARM3 V2` roster from P0C1:

- MU1 is the canonical publisher's usual `PacketTemplate` and `SvFixedPointSineEngine`.
- MU2 and MU3 each have an **independently prebuilt packet buffer, separate DDS engine and separate snapshotted signal state**. Packet templates are built once at START, outside the 4kHz hot path.
- Each stream's encoded destination multicast MAC, `svID`, VLAN and APPID are taken unchanged from the independently validated V2 profile (including the reference SCD's same APPID `0x4000`).
- Each tick's three frames use the **same** `smpCnt` and one existing `smpSynch` decision; the M4 shared-grid planner checks common slot count/epoch and revokes group TX on phase/session inconsistency. There is one physical TX queue, so packets leave sequentially.
- `pending_ticks>1` advances the phase engines by missed slots and skips stale frames; each canonical send participates in the existing bounded Ethernet failure / fail-closed logic.
- Diagnostic mirrors/probes are disabled in three-SV mode, instead of multiplying debug traffic by three.

## Engineering activation sequence

This is a **lab preview**, not yet a GUI-exposed feature:

1. On an isolated bench, flash the **paired M4 feature-branch firmware**.
2. With SV STOPPED, send `MULTISV BEGIN / CHUNK / VALIDATE / COMMIT` with the V2 envelope built from the three-IED SCD. Confirm `MULTISV SHOW` reports the exact 3 MACs/svIDs.
3. Send `MULTISV SELECT`. No SV packets are sent until START is accepted. `MULTISV DESELECT` returns the next START to the original single-publisher mode; neither command is allowed while SV is running.
4. Follow the **existing** `CFG BEGIN / CFG COMMIT / CFG SHOW / START` transaction/boot-token handshake and verified Ethernet LINK UP. Selection is independent of that verified start authority. The old STOP command stops the one common publisher task.
5. With a test capture NIC, examine EtherType `0x88BA` for `MU01_SV1`, `MU01_SV2`, `MU01_SV3` and MAC suffixes `01/02/03`. Do not infer 12k measured frames/s from CI compile success: collect the actual PHY packet capture and TX lateness counters.
6. Use `MULTISV DESELECT` while STOPPED to return to exactly the old single-SV publisher semantics.

The host SCL compiler chooses the supported `9-2LE` timing family and resolves each 4I4V DataSet; ESP firmware **does not parse SCD/XML**.

## Limits, and P0D/P0E next

- **Setpoints:** MU2/MU3 currently own *independent snapshots* of the same initial 4I4V bank at START. They are **not yet independently editable** by Studio while running. Per-MU atomic setpoint transactions and stream-specific STOP/START are P0D, before release.
- **PTP:** One hardware clock drives all three sample times, and the three frames receive one shared existing firmware `smpSynch` decision; a correct `smpCnt` does not in itself prove synchronization to an external GM. Internal bench SOURCE mode remains the practical default.
- **Live TX performance:** 12,000 SV frames/s is the *intended* 3×4000 nominal traffic requirement, **not a measured result** until the physical ESP32-P4 capture. 250 µs deadline, TX descriptor headroom and PTP coexistence need a normal Wireshark/Npcap bench experiment, not expensive dedicated hardware.
- **GUI:** single-window MU1/MU2/MU3 pagination and SCL-import/deploy UI remain to be wired in P0E.
- **Compatibility:** no change to the official M3.H.3 release files or V1 `PROFILE` binary format. M4 selectable TX is a separate firmware capability `MULTISV-TX-LAB-PREVIEW`, not the final `SV-MULTI3-V1`.

**Next:** protect per-MU signal banks with generation-specific atomic commits (P0D), then bind Studio Table/Waveform/Phasor via a three-row roster/scenario model (P0E). Release only a matched Studio+firmware M4 package after capture-driven optimization and repeatable successful real SV traffic.
