# First SV packet — independent receiver check

Use this when ARStack Studio reports SV publishing but ARSAS or DigSubAnalyzer shows no Sampled Values. The goal is to identify whether the frame is **missing from the receiving Ethernet interface** or **arriving but not being decoded**.

## 1. Confirm SV is actually publishing

Connect the ESP32-P4 to an **isolated laboratory Ethernet network**. Open ARStack Studio and confirm **SV RUNNING** with increasing transmit/frame counts. PTP RUNNING alone does not mean SV frames are being transmitted.

## 2. Capture on the receiver's Ethernet adapter

On the same Windows machine used for the analyzer:

1. Select the **physical Ethernet adapter** connected to the ESP32-P4 laboratory network, rather than Wi-Fi or the Npcap loopback adapter.
2. In Wireshark, start a new capture **with no capture filter**. A capture filter is different from a display filter.
3. While Studio is publishing, apply the **display filter** `eth.type == 0x88ba || vlan.etype == 0x88ba`. This covers both plain SV Ethernet frames and SV inside an 802.1Q priority/VLAN tag. The outer `eth.type` may be `0x8100` for tagged frames, so `eth.type == 0x88ba` **alone is not sufficient**.
4. Record whether SV frames appear and whether packet details show `APPID`, `svID`, `smpCnt`, destination MAC and, where present, VLAN ID/PCP. Check the actual packet bytes, not only the configured SCL; NIC VLAN offload/stripping can change what a host capture exposes.
5. Save **3–5 seconds** of traffic as `.pcapng` or `.pcap` with **no capture filter**, along with the selected NIC name, physical topology, Studio/firmware versions, analyzer versions and chosen stream filters. Remove confidential project information before sharing.

The reference M3.H profile uses APPID `0x4000`, destination `01:0C:CD:04:00:01` and approximately **4,000 frames/s**. The stream ID depends on the profile deployed by Studio; do not assume a specific `svID` without examining the wire.

## Windows P0 test binaries (25 October 2026)

Use the **exact artifact and source branch** below. A passing decoder unit test does not prove live reception, and historical stable releases predate some receive-side diagnostics.

| Component | Correct physical-test download | Build source and scope |
| --- | --- | --- |
| ARStack Studio preview | [Windows GUI preview artifact](https://github.com/masarray/arstack61850/actions/runs/40134517100/artifacts/12576288162) | PR #325 head `83439a4`, verified native Qt/Inno preview and matched preview firmware, not M3.H.1 stable |
| ARSAS | [Portable P0 artifact](https://github.com/masarray/arsas/actions/runs/40104371705/artifacts/12563917472) | `main` `2bdb6c0` including **First received SV stream**; **do not substitute ARSAS v1.6.41 stable or the diverged 1.6.42 RC preview branch**, which lack that exact feature |
| DigSubAnalyzer / Process Bus Insight | [v1.4.2 portable package](https://github.com/masarray/DigSubAnalyzer/releases/download/v1.4.2/ProcessBusInsight-v1.4.2-win-x64-portable.zip) | Physical receive baseline; subsequent PR #55 added a *synthetic* VLAN 0/untagged decoder test but did **not** modify the runtime decoder |
| ARStack shipped M3.H.1 baseline | [Official unchanged Windows Setup/Portable and firmware](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h1-20261024) | Frozen stable baseline; preserve this installed Studio/firmware pair when comparing with the preview |

GitHub Actions artifacts download as ZIPs and may expire. Extract the packages and use their contained executables; do not rename a candidate build as stable. The ARStack Studio **preview firmware is paired with its own preview build**. Do not mix it with M3.H.1 firmware or overwrite a successful stable installation unintentionally.

## 3. Compare VLAN-tagged with untagged laboratory SV

The included **reference SCL** requests Ethernet **802.1Q priority tagging**: VLAN ID `0`, PCP `4`. Some PC network or switch paths handle priority-tagged traffic differently.

For diagnosis only, an alternative [untagged 4I+4V SCL](../apps/arstack_studio/templates/arstack_4i4v_untagged_lab.scd) keeps the same APPID, multicast MAC, data-set and sample rate **but omits VLAN tagging**. **Download the .scd file separately**: it is not a second built-in GUI reference preset. Save the [raw untagged SCL](https://raw.githubusercontent.com/masarray/arstack61850/main/apps/arstack_studio/templates/arstack_4i4v_untagged_lab.scd) locally (for example, as `arstack_4i4v_untagged_lab.scd`).

In the **Studio preview**, stop publishing first, then:

1. Import/Open that local SCL, select its 4I+4V SV stream and open the **Profile Inspector**.
2. Under **Profile Context**, explicitly choose **Legacy 9-2LE**. External SCL files do *not* automatically inherit the built-in reference profile-family policy.
3. Under **Counter Policy**, enter `4000` and press **Confirm**; this reproduces the validated reference `smpCnt` modulus `0..3999` for this isolated A/B case. Generic SCL cannot silently infer it.
4. Check that the inspector shows **CLASS A**, **VLAN untagged**, **APPID 0x4000**, **64 B / 16 leaves**, **4,000 frames/s**, **1 ASDU** and ESP32-P4 support before any device deployment.
5. Deploy the profile to the **matched preview firmware**, verify readback/accepted configuration where offered, start SV and take a fresh receiver-side capture on the **same NIC**. Compare with the priority-tagged reference without changing waveforms, NIC or cable path.

If Studio shows a blocked or Class B profile, **do not force START**; inspect the family, modulus and diagnostic messages instead. This test is not a guarantee that your NIC receives untagged or priority-tagged traffic. The original M3.H.1 release remains immutable.

**Do not change the VLAN scheme on an operating substation network.** This is an isolated-bench comparison to identify an Ethernet capture path difference; a change in receive behavior is evidence about the path, not proof of an arbitrary SV protocol fault.

## 4. Interpret the result

| What the receiver PC observes | Next engineering action |
| --- | --- |
| No plain or VLAN-encapsulated SV frames on the selected adapter | Check board link, Studio TX state, adapter choice, switch mirror/TAP, NIC VLAN offload and tagged VLAN forwarding. First confirm the combined display filter was used. |
| SV appears in Wireshark but not in DigSubAnalyzer | Note raw frame and decoder counters and share the capture; inspect Npcap filter and raw BER path. |
| SV appears in Wireshark but ARSAS snapshot stays empty | Use ARSAS's **First received SV stream** option (when available), or match selected SCL APPID/MAC/`svID` to the actual packet. |
| SV decodes in both applications | Compare samples, `smpCnt` rollover and waveform plausibility, then continue to timing/interoperability checks. |

Open [P0 receiver acceptance issue #324](https://github.com/masarray/arstack61850/issues/324) for incident tracking. A self-decoded PCAP from Studio cannot replace an **independent RX capture from the affected NIC**.
