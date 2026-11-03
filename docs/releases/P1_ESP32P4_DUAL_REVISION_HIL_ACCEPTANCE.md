# P1 ESP32-P4 dual-revision — physical acceptance (engineering preview)

**Scope:** Waveshare ESP32-P4-ETH-M v3.2 and previously accepted ESP32-P4 v1.3. **Status:** PHYSICAL TESTING PENDING. No energized process bus, no live protection system, no externally traceable PTP claim. Existing M3.H.3 stable evidence is not v3.2 evidence.

Use the **matched Windows Native Preview** or **paired Linux Preview AppImage** from the same PR build run, not the standalone GUI-only Linux artifact. A valid test package contains Studio, bundled espflash v4.5.0, `firmware-manifest.json`, `firmware-catalog.json`, separate `pre-v3` / `v3.x` binaries, and `SHA256SUMS.txt`. Record the manifest `sourceCommit` and both image digests.

## Operator procedure (one isolated board at a time)

1. Disconnect from energized equipment. Connect the board USB; keep Ethernet connected only to an isolated bench switch/capture port. Use external board power if required by vendor instructions. Record PCB model and actual silicon revision.
2. Launch Studio. Confirm that passive detection does not reset, write firmware, stop an autonomous board or start SV/PTP. For a fresh device, use **Set up board** and acknowledge the qualified PCB/isolation check. For an existing verified device, choose **Update firmware**.
3. Studio performs an authorized ROM probe. On v1.3 it must select **pre-v3**; on v3.2 it must select **v3.x**. Check ROM factory eFuse MAC against the application `device_id` in copied diagnostics. Unknown/ambiguous revision, invalid MAC or unsupported board must be rejected before write.
4. Keep USB connected until installation ends. Verify that Studio resets/reacquires the *same* physical board and reports a fresh `boot_id`, expected firmware version/protocol/capabilities and the **exact** manifest build ID. A 100% flash bar by itself is not Ready.
5. Check PHY link and standalone board behavior. With isolation maintained, explicitly START the canonical 4I+4V/4000fps test. Capture Ethernet SV packets and PTP Layer-2 frames. Verify 4000 frames/s per configured MU, correct `smpCnt` sequence (including wrap), expected VLAN/APPID/quality, no loss/duplicate frames, and truthful sync state. Do not interpret simulated GM as physical UTC/GNSS traceability.
6. Verify STOP, cold boot, USB disconnect/reconnect, COM renumbering, and bounded recovery. No silent SV/PTP restart or cached SCD/profile deployment after reconnect.
7. Exercise controlled negative scenarios on isolated hardware: unexpected USB removal, explicit Download mode, failed flash, and recovery. Record whether content may be partially written; never force-write a deliberately incompatible revision or change eFuses/security to make a test pass.

## Evidence sheet (complete separately for **each** silicon revision)

| Evidence | v1.3 | v3.2 |
| --- | --- | --- |
| Board model / PCB rev / silicon ROM rev | PENDING | PENDING |
| ROM factory MAC = postboot device ID | PENDING | PENDING |
| Tool version + OS + preview artifact ID / sourceCommit | PENDING | PENDING |
| Selected image name + manifest SHA + image-header bounds | PENDING | PENDING |
| Fresh boot ID + exact postboot firmware build/protocol | PENDING | PENDING |
| PHY link + captured 4kHz SV with `smpCnt`/quality continuity | PENDING | PENDING |
| PTP Layer-2 capture + defensible `smpSynch` evidence | PENDING | PENDING |
| STOP/START, reset/reconnect, no silent restart | PENDING | PENDING |
| Interrupted write and bounded recovery | PENDING | PENDING |

Attach Wireshark PCAP/PCAPNG, serial diagnostics, ROM/flash tool output, OS/build SHA and notes about any nonconformance. Mark **PASS** only after reproducible measured evidence; CI artifacts and simulations are never physical evidence. v3.2 remains an **engineering preview** until qualified in hardware on both lanes.

See [PRD](../PRD_REVISION_AWARE_ESP32P4_FIRMWARE.md) and [ARD](../ARD_REVISION_AWARE_ESP32P4_FIRMWARE.md) for mandatory negative tests and release gates.
