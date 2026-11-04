# PRD — Revision-aware ESP32-P4 Firmware Setup in ARStack Studio

**Status:** Approved product direction; implementation NOT complete. **Date:** 2026-11-03. **Priority:** P1 public hardware compatibility blocker. **Owner:** ARStack Studio / ESP32-P4 Process Bus.  
**Companion architecture:** [ARD_REVISION_AWARE_ESP32P4_FIRMWARE.md](ARD_REVISION_AWARE_ESP32P4_FIRMWARE.md). **Rules:** [AGENTS.md](../AGENTS.md).

## 1. Problem and outcome

A public user reports that a newly purchased Waveshare ESP32-P4-ETH-M (silicon v3.2) cannot boot the firmware bundled with M3.H.3. The currently shipped firmware was compiled for pre-v3 silicon, while Studio also intentionally refuses chip revisions >= v3.0. An electrical engineer should not need ESP-IDF, Python, command lines, firmware file selection, revision knowledge, or bootloader expertise to operate ARStack.

**Target experience:** Connect a supported board, open Studio, select a single contextual action (Set up board or Update firmware), and let Studio safely identify, select, install, restart, and verify the *correct* signed/trusted packaged firmware. Standard use is offline and self-contained. Never start flashing solely because a serial device appears.

**Value promise:** No guessing and no wrong-revision flash. A success message means the intended firmware has actually booted and identified itself; not merely that bytes were sent.

## 2. User experience contract

**Primary persona:** protection / substation automation / commissioning engineer who is comfortable with USB and Ethernet, but not software development.

| Observed condition | Main UI | Operator action |
| --- | --- | --- |
| No usable board | 'Connect your ESP32-P4 board' | Connect USB; automatic non-disruptive discovery |
| Verified ARStack board, exact supported build | 'Board ready' | None; continue engineering |
| Verified board, supported update available | 'Firmware update available' + **Update firmware** | One click; preflight below |
| Fresh/unsupported-app board on an explicitly chosen device | **Set up board** | One click; guided recovery/ROM probe |
| Device is publishing SV or emitting PTP | 'Board is in use' | Explicit operator confirmation of interruption and safe STOP proof before flashing |
| Multiple candidate boards/ambiguous USB identity | 'Choose the board to set up' | Choose one physical board; never guess by COM number |
| Unsupported silicon, unknown PCB, unreadable revision | 'This board is not supported yet' | No write; Show details for technical reason |
| Recovery/download mode required | 'Put the board in download mode' | Short illustration/text for BOOT/RESET and **Try again** |
| Setup complete | 'Board ready' | Return to the prior engineering screen; no automatic SV/PTP START |

**Noise budget:** One compact status line, one primary action, optional expandable 'Details' / 'Copy diagnostics'. No prominent revision numbers, tool names, hashes, COM ports, PTP internals, raw errors, or many persistent steps during the normal path. In Details, provide board model evidence, silicon revision, selected firmware version/build, exact error code, stage, bounded timestamps, and support-ready report. Show percentage only when real flasher progress is available and monotonic. Otherwise use a neutral 'Installing firmware…' state, not an invented 0–100%.

**Single-action does NOT mean silent destructive action.** The click explicitly authorizes flashing one identified board after preflight; running SV/PTP needs an additional clear confirmation that tests will stop. Startup, reconnect, and detection alone may never reset, erase, flash, stop, or reconfigure a running board.

## 3. P0 must-have requirements

- **R01 — Revision-aware offline bundle.** One Studio distribution includes independent *pre-v3* and *v3.x* ESP32-P4 firmware builds with separate exact-build manifests; no developer toolchain required on end-user PCs. Initial v3.2 release remains a labeled preview until physical acceptance. Preserve M3.H.3 official release/tag/assets unchanged.
- **R02 — Authoritative silicon probe.** Upon authorized setup, read ESP32-P4 chip revision from the ROM/flasher, not from COM number, GUI guess, board marketing name, or any application log. Recognize supported major/minor revisions and reject unknown/contradictory evidence.
- **R03 — Deterministic selection.** Exactly one firmware compatible with chip, qualified board/PHY profile, revision range, release channel, and protocol is selected. Zero or multiple matches => fail closed. Firmware pre-v3 can never be written to v3.x and vice versa.
- **R04 — Package integrity & provenance.** Verify manifest schema, expected target/revision/profile, bundle file inventory, image length, SHA-256, exact source/build identity and embedded bootloader/application image revision compatibility *before any write*. SHA-256 alone is not authenticity: distributions must be release-provenanced; future remote distribution requires signature verification with a pinned trust anchor.
- **R05 — One port owner.** Reuse SmartSessionController -> DeviceController/DeviceIoWorker serial release acknowledgement -> FirmwareManager/FirmwareWorker. Exactly one firmware tool/process and one active device operation; no parallel COM consumers or cloned updaters.
- **R06 — Non-disruptive startup.** Prefer a bounded ARStack application `IDENTIFY`. Silence/timeout means unidentified, NOT 'empty flash'. ROM probe/reset only on user intent; do not auto-stop autonomous SV/PTP, and do not let firmware recovery bypass M4 board/roster authority.
- **R07 — Bounded operation.** Every stage has a deadline and one terminal outcome; an absolute elapsed-time deadline prevents an endless spinner even if partial output/events keep arriving. No unbounded retries, polling, or callback accumulation. Terminal errors retain one relevant Retry action.
- **R08 — Truthful verification.** Flash tool exit 0 is only 'write reported complete'. After reset, re-enumerate safely, require semantic ARStack `IDENTIFY` with exact expected firmware build/protocol/capabilities and device binding before showing Ready. Mismatch or no boot => 'Setup not verified'.
- **R09 — Recovery.** USB unplug / timeout / crash during write enters explicit 'Needs recovery' outcome. No blind second flash, erase-all, `--force`, unsafe eFuse change, or claim of automatic rollback from a merged raw image. Operator-guided ROM recovery must remain possible.
- **R10 — Existing-product parity.** V1 4I+4V/4000 fps, PTP, autonomy, fail-closed STOP and M4 safety gates behave as before; a firmware refresh never implies START or silently deploys cached drafts.
- **R11 — Cross-platform identical policy.** Windows installer/portable and Linux AppImage carry the same revision selection/catalog semantics and truly compatible pinned flashing executables. No runtime network download or user-managed Python/ESP-IDF.
- **R12 — Honest compatibility.** Silicon revision compatibility does not prove board/PHY wiring compatibility. Only explicitly qualified board hardware profiles may be offered automatic setup; unknown boards require an explicit supported-model assessment, never an inferred 'all ESP32-P4 work' claim.

## 4. Exceptional paths and example UI copy

| Error class | Compact message | Primary next action |
| --- | --- | --- |
| USB absent / port busy | 'Board connection lost' | Reconnect / Try again |
| Cannot establish ROM identity | 'Could not identify this board' | Download-mode help + Try again |
| Unsupported silicon/PCB | 'This board is not supported yet' | View supported boards |
| Missing/ambiguous/corrupt package | 'Firmware package could not be verified' | Reinstall Studio / Details |
| Still transmitting | 'Stop the running test before updating' | Stop and continue (explicit) |
| Writing interrupted | 'Firmware setup was interrupted' | Recover board |
| Reconnect but unexpected build | 'Firmware setup could not be verified' | Try recovery |
| Correct verified build | 'Board ready' | Continue |

Action wording is illustrative; compact 9–12px normal typography, no big banners or heavy cards. Errors are friendly at surface, deterministic typed codes in Details; never claim success on a stale session or earlier boot.

## 5. Acceptance matrix (no shortcuts)

| ID | Required evidence | Acceptance |
| --- | --- | --- |
| U01 | Connected v1.3 board with current firmware | Ready without reset/ROM probe/flash |
| U02 | Fresh supported v1.3 board | Legacy image selected and boots; no v3 image write |
| U03 | Fresh Waveshare v3.2 board | v3.x image selected, boots, exact-build postflight |
| U04 | Forced test with opposing images | Every wrong revision rejected before first write |
| U05 | Unknown revision or unsupported PCB | No flash and actionable support message |
| U06 | Corrupt image/manifest, missing binary, duplicate match | No flash; precise typed failure |
| U07 | Multiple USB devices and COM reassignment | No cross-device flash / wrong-device verification |
| U08 | Running autonomous SV or active PTP | No silent STOP, reset, or firmware operation |
| U09 | Unplug during each stage; failure while writing | Bounded error; safe guided recovery, no false Ready |
| U10 | Flasher exits 0, firmware does not boot or has wrong build | Not successful; evidence retained |
| U11 | Windows and Linux packaged offline install | Same exact firmware catalog, verified flasher, no SDK dependency |
| U12 | Reinstall/upgrade legacy Studio | Old M3.H.3 unchanged; no legacy package contract regression |
| U13 | One-hour idle / 100 repeated connect-disconnect simulations | Bounded memory/handles; no duplicate workers, phantom dialogs or permanent busy |
| U14 | Physical SV/PTP acceptance on v3.2 | 4I+4V canonical 4kfps, counters/quality, Ethernet/PTP frames and timing independently captured; failures reported honestly |

CI can verify U01–U13 in host simulations and build contracts as applicable, but U02/U03/U08/U09/U14 require actual representative boards. v3.2 **preview** may ship with an explicit unverified hardware gate; **stable** cannot be claimed until HIL evidence is captured. Do not imply PTP UTC/GNSS traceability.

## 6. Release phases and priorities

- **P1A (highest impact):** ARD/PRD/AGENTS and compatibility disclosure; baseline exact stable untouched.
- **P1B:** independent binary build profiles with fail-closed manifest and revision/header CI; pinned flasher v3.2 suitability proven.
- **P1C:** single-owner Studio catalog selection, guided one-action installer, bounded recovery and strict postflight proof.
- **P1D:** physical v1.3 + v3.2 lab acceptance, public v3.x engineering preview, promotion only after evidence.

**Out of scope:** universal third-party ESP32-P4 boards, cloud OTA, unattended update, automatic cross-machine device migration, promises of power-fail atomicity for raw merged flashing, or bypass of secure-download/security restrictions.

## 7. Canonical external evidence

- Espressif chip revision compatibility: https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32p4/api-reference/system/chip_revision.html
- Espressif PCN v3.2 (separate binary / `ESP32P4_SELECTS_REV_LESS_V3=n`): https://documentation.espressif.com/en/PCN202600801_ESP32-P4_Chip_Revision_v3.2_Upgrade_Chip_Revision_v1.3_Demand_Collection_and_EOL_Plan_Description.html
- Espressif AR2026-004 flasher + v3.2 watchdog advisory: https://documentation.espressif.com/AR2026-004_Compatibility_Advisory_for_ESP32-P4_Chip_Revision_v3.2_EN.html
- Waveshare qualified build profiles: https://github.com/waveshareteam/ESP32-P4-Platform/blob/main/docs/ESP32P4_REVISION_CONFIG.md
- Espressif esptool incompatible-image rejection: https://docs.espressif.com/projects/esptool/en/latest/esp32p4/esptool/basic-commands.html
- NCSC firmware update authenticity guidance: https://www.ncsc.gov.uk/collection/device-security-principles-for-manufacturers/provide-updates-securely
