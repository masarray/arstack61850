# First SV packet — independent receiver check

Use this when ARStack Studio reports SV publishing but ARSAS or DigSubAnalyzer shows no Sampled Values. The goal is to identify whether the frame is **missing from the receiving Ethernet interface** or **arriving but not being decoded**.

## 1. Confirm SV is actually publishing

Connect the ESP32-P4 to an **isolated laboratory Ethernet network**. Open ARStack Studio and confirm **SV RUNNING** with increasing transmit/frame counts. PTP RUNNING alone does not mean SV frames are being transmitted.

## 2. Capture on the receiver's Ethernet adapter

On the same Windows machine used for the analyzer:

1. Select the **physical Ethernet adapter** connected to the ESP32-P4 laboratory network, rather than Wi-Fi or the Npcap loopback adapter.
2. In Wireshark, start a new capture **with no capture filter**. A capture filter is different from a display filter.
3. While Studio is publishing, apply the **display filter** `eth.type == 0x88ba`.
4. Record whether SV frames appear and whether the packet details contain `APPID`, `svID` and `smpCnt`.
5. Save **3–5 seconds** of traffic as `.pcapng` or `.pcap` and remove any confidential project information before sharing.

The reference M3.H profile uses APPID `0x4000`, destination `01:0C:CD:04:00:01` and approximately **4,000 frames/s**. The stream ID depends on the profile deployed by Studio; do not assume a specific `svID` without examining the wire.

## 3. Compare VLAN-tagged with untagged laboratory SV

The included **reference SCL** requests Ethernet **802.1Q priority tagging**: VLAN ID `0`, PCP `4`. Some PC network or switch paths handle priority-tagged traffic differently.

For diagnosis only, an alternative [untagged 4I+4V SCL](../apps/arstack_studio/templates/arstack_4i4v_untagged_lab.scd) keeps the same APPID, multicast MAC, data-set and sample rate **but omits VLAN tagging**. In Studio, stop publishing, load this SCL, select/deploy its SV stream and restart. Compare the receiving capture against the original configuration.

**Do not change the VLAN scheme on an operating substation network.** This is an isolated-bench comparison to identify an Ethernet capture path difference; a change in receive behavior is evidence about the path, not proof of an arbitrary SV protocol fault.

## 4. Interpret the result

| What the receiver PC observes | Next engineering action |
| --- | --- |
| No `0x88BA` frames on the selected adapter | Check board link, Studio TX state, adapter choice, switch mirror/TAP and tagged VLAN forwarding. |
| SV appears in Wireshark but not in DigSubAnalyzer | Note raw frame and decoder counters and share the capture; inspect Npcap filter and raw BER path. |
| SV appears in Wireshark but ARSAS snapshot stays empty | Use ARSAS's **First received SV stream** option (when available), or match selected SCL APPID/MAC/`svID` to the actual packet. |
| SV decodes in both applications | Compare samples, `smpCnt` rollover and waveform plausibility, then continue to timing/interoperability checks. |

Open [P0 receiver acceptance issue #324](https://github.com/masarray/arstack61850/issues/324) for incident tracking. A self-decoded PCAP from Studio cannot replace an **independent RX capture from the affected NIC**.
