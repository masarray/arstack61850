# ARD — Fail-closed Revision-aware ESP32-P4 Firmware Provisioning

**Status:** Architecture accepted for implementation; NOT implemented or hardware accepted. **Date:** 2026-11-03. **Scope:** Studio + ESP32-P4 firmware packaging, Windows/Linux.  
**Product contract:** [PRD_REVISION_AWARE_ESP32P4_FIRMWARE.md](PRD_REVISION_AWARE_ESP32P4_FIRMWARE.md). **Agent invariants:** [AGENTS.md](../AGENTS.md).  
**Release invariant:** immutable official stable `processbus-sim-m3h3-20261025` and existing M4 workstreams; no timing/PHY patch as an incidental part of provisioning.

## A. Audited repository baseline (2026-11-03, main `071dae5`)

| Existing owner | Verified code/contract | Extension strategy |
| --- | --- | --- |
| Firmware build | `embedded/esp32p4_smv_injector/sdkconfig.defaults` sets `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`; bench v1.3 | Keep known-good pre-v3 profile; add isolated v3.x profile, never mutate one shared generated sdkconfig |
| CI | `.github/workflows/esp32p4-smv-injector.yml`, `espressif/idf:v5.5.4`, checks pre-v3, merges raw image at flash offset 0 | Targeted two-profile variant within existing pipeline, immutable per-profile outputs and image/header checks |
| Manifest | `arstack.studio.firmware.v1`, single `chipRevisionPolicy=pre-v3`, source SHA/build ID, SHA-256 | Explicitly versioned v2 multi-image catalog; retain strict v1 read compatibility for old stable pre-v3 only |
| GUI supervisor | `SmartSessionController.cpp` owns firmware stage, one serial owner, release generation, reconnect, failure | Extend existing authority; no QML-owned alternative installer |
| Device session | `DeviceController`/`DeviceIoWorker` typed `DeviceIdentity` and boot identity, serial release | Preserve current semantic-first non-disruptive detection |
| Flasher | `FirmwareManager.cpp` uses asynchronous `FirmwareWorker` with external `espflash` `board-info` / `write-bin` / reset | Verify flasher revision v3.2 support *on hardware* before selecting/upgrading; exact pinned tool; same worker/CLI boundary |
| Revision guard | `supportsEsp32P4Revision(major,minor)` requires `major < 3` and manifest equals `pre-v3` | Replace *policy* with deterministic catalog/revision proof; never widen guard without a matching verified firmware |
| Bundled tools | `arstack-studio-release.yml`, Linux AppImage use `espflash v4.5.0` | Do not assume advisory for Espressif Python `esptool` guarantees behavior of Rust `espflash` |
| Physical acceptance | M3.H.3 has real v1.x SV/PTP evidence; v3.2 untested | Independent v3.2 hardware smoke and wire/PTP capture are mandatory for stable claim |

The current `FirmwareManager` has per-operation limits (15 s launch, 30 s ROM probe, 180 s write, 20 s reset) and bounded reconnect attempts. Preserve these useful safeguards; add a transaction-level hard deadline and proof of catalog/device identity. Existing Studio currently auto-STOPs SV when the user begins an update: introduce explicit acknowledgment when an active SV *or PTP* experiment would be interrupted. Never reset a RUNNING autonomous publisher through passive discovery.

### Hardware constraints backed by vendor evidence

- ESP32-P4 pre-v3 and v3.x require separate binaries because of incompatible ROM/ABI assumptions. Do not force a v1.x bootloader on v3.x. `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=n` is required for v3.x; set `CONFIG_ESP32P4_REV_MIN_300=y` with the IDF-supported profile and inspect computed min/max image headers. For pre-v3, retain the previously accepted selector and inspect actual built revision bounds.
- Espressif requires appropriate tool support for v3.2 flash power-on. `espflash v4.5.0` release notes include ECO6/ECO7 flash power-on changes, but compatibility with *the pinned bundled executable, board, OS and exact flash flow* is a test gate, not an assumption.
- Espressif v3.2 WDT advisory: evaluate ESP-IDF release/v5.5 v5.5.5+ (or suitable maintained patch level) in the new v3.x lane, particularly if external 32 kHz crystal is configured. Toolchain upgrades must be independently proven on legacy lane; do not casually rebuild or rewrite official M3.H.3.
- Silicon identity != PCB wiring identity. A true v3.2 chip does not prove Waveshare board pinout, PHY/reset circuitry, flash capacity or power suitability. Catalog must carry a qualified board profile; apply conservative supported-board evidence, not a generic "ESP32-P4" whitelist.
- `esptool` rejects invalid revision headers at bootloader offset unless forced. Studio's runtime checks are an additional protection; never use `--force` to bypass incompatible chip or security checks.

## B. One authoritative flow / ownership

`SmartSessionController` remains the **sole state and authorization owner** (operator intent, STOP authorization, session generation, USB candidate binding, deadlines, terminal result). `DeviceIoWorker` remains the **sole QSerialPort owner**; `FirmwareWorker` remains the **sole external flasher process owner** with exclusive port handoff. `FirmwareManager` owns package validation, ROM results, catalog selection and install proof. Introduce at most one small **pure, no-thread/no-I/O firmware catalog selector** shared between Studio tests and production; do not fork `FirmwareManager` or add redundant state/pollers.

Proposed typed boundaries:

    FirmwareImage {
      profileId; family; minRevision; maxQualifiedRevision;
      supportedBoardProfile; image; flashOffset; length; sha256;
      idfVersion; flasherContract; buildId; sourceCommit; capabilities;
      bootloaderHeaderBounds; applicationHeaderBounds;
    }
    FirmwareCatalog { schema; releaseChannel; images[]; provenance; }
    RomIdentity { chip; major; minor; romChipIdOrMac?; observedUsbFingerprint; }
    QualifiedSelection { exactImage; romIdentity; supportProof; catalogHash; operationGeneration; }
    FirmwareAttempt { attemptId; sessionGeneration; portLease; stage; deadline; selectedImage; terminalStatus; }

The object names are design types, not demands for exact class names. There must be a single immutable `QualifiedSelection` captured at end of preflight. Every later step (write, reset, postflight) checks current attempt/generation/lease against it. Cache manifest hash and image digest per immutable packaged file; invalidate on package path/content/mtime/size change and verify at the write boundary to avoid TOCTOU. Do not trust a user-replaced writable catalog by its checksum alone.

### Manifest v2, backward compatibility, and packaging

Version `arstack.studio.firmware.v2` describes an array of *two disjoint profiles*, not one mutable 'current' .bin. Example conceptual inventory:

| Profile | Qualified silicon | IDF profile | Binary |
| --- | --- | --- | --- |
| `esp32p4-pre-v3` | explicitly qualified existing v1.x (including v1.3) | `SELECTS_REV_LESS_V3=y` | `arstack-esp32p4-prev3.bin` |
| `esp32p4-v3x` | separately qualified v3.2; other minor v3.x only after validation | `SELECTS_REV_LESS_V3=n` + `REV_MIN_300=y` | `arstack-esp32p4-v3x.bin` |

No overlap in **qualified selection**; max hardware revisions may be tighter than Espressif's theoretical `REV_MAX_FULL`. Reject missing/duplicate filenames, path traversal, malformed hashes, wrong target, unsupported/unknown schema, invalid lengths/offsets, duplicate overlapping selectors, release mix-ups, missing tool contract, or missing image-header evidence. A v1 manifest is recognized only with its legacy pre-v3 semantics; never "upgrade" its policy implicitly to support v3.x.

Generate each firmware in **a separate SDKCONFIG + build/output directory**, with exact source SHA and locked toolchain. Do not let pre-existing `sdkconfig` silently override profile defaults; verify actual generated `sdkconfig` and both app+bootloader `esp_image_header_t` min/max revision fields, chip ID, partition/offset layout, merged binary SHA and per-image provenance before packaging. Include `firmware-manifest.json` and both binary hashes in Windows/Linux bundles; packaged self-check ensures exactly two valid images and correct versioned catalog. No runtime compilation, no network dependency. Explicit stable vs preview channel in catalog; do not silently select unaccepted v3.x stable.

Trust policy: packaged, immutable release asset + SHA-256 currently validates local integrity, **not cryptographic origin by itself**. For remote catalogs/update services in the future, require an authenticated signed manifest and pinned verification key. No network auto-update in this milestone; fail closed on modified/unrecognized local bundle.

## C. Exact device provisioning state machine

    PASSIVE_DISCOVERY -- app IDENTIFY verified --> READY / OFFER_UPDATE
           |
           +-- no response --> UNIDENTIFIED (never infer blank firmware)
           |
           +-- user SET UP / UPDATE authorization
                   |
                   v
             PREFLIGHT / QUIESCE (explicit permission if SV/PTP running)
                   |
                   v
             RELEASE_SERIAL -- portReleased(generation) --> ROM_PROBE
                   |
                   v
             REVISION_AND_BOARD_PROOF --> SELECT_EXACT_IMAGE --> VALIDATE
                   |
                   v
             WRITE_IN_PROGRESS --> WRITE_REPORTED_COMPLETE
                   |
                   v
             RESET --> USB_REACQUIRE --> POSTBOOT_IDENTITY_PROOF --> READY
                   |
             any failed gate --> TERMINAL_ERROR / GUIDED_RECOVERY

**Entry rule:** Desktop startup and serial reconnect perform only bounded application `IDENTIFY`; never invoke `board-info` if that requires a reset as a side effect. Unknown application identity is not permission to take over a potentially autonomous RUNNING board. ROM probing requires a fresh operator action for the selected candidate. Do not automatically flash because USB is present.

**QUIESCE rule:** If SV/PTP is RUNNING, stop only after explicit confirmation of test interruption, then obtain authoritative STOPPED evidence for **all relevant producers**. If current board state cannot be proven (unknown firmware / disconnected management), present a recovery notice requiring operator confirmation the board is disconnected from any live process bus; no blind assumption. Existing M4 `MULTISV` roster, SCD/board state, and STOP authority must not be overwritten through fallback V1 profile.

**Port-binding rule:** Accept `portReleased(generation)` before ROM, bind attempt to selected device/USB fingerprint and session, and never use COM3/COM4 text alone as an identity. On re-enumeration use semantic eFuse-derived `device_id` where known, plus ROM identity, USB serial/location evidence as available. If ROM ID cannot be mapped directly to `device_id`, do not invent equality; compare only proven independent attributes and refuse ambiguous same-bus replacement. Multiple candidates => explicit selection. No second watcher or serial owner.

**ROM rule:** Use tested pinned flasher to obtain structured chip+revision when available. If only textual `board-info` exists, isolate a strict, bounded parser for the known pinned-version output with positive/negative fixture tests; do not guess revision from a failed command, partial log, or misleading error text. Preflight board profile and security constraints independently.

**Selection rule:** `matches` must return exactly one qualified image by verified revision, approved board profile, release channel, and protocol; if not, block. A CHIP family mismatch, unknown v4.x future revision, unknown minor outside qualification, or mismatched IDF image header must be rejected before first write.

**Write rule:** One process, one port, one selected immutable image, no `--force`, `erase-all`, eFuse changes, loose fallback command, or background second attempt. Mark stage `WRITE_IN_PROGRESS` before launching process; treat interruption after first byte as **uncertain flash content**. Do not claim automatic rollback/atomicity for raw merged images. Cancellation before write is harmless; after write begins display guarded state and allow failure/recovery rather than claiming a safe instant cancel. Never automatically retry a possibly partially written image.

**Proof rule:** Tool exit 0 != product success. Reset, wait for bounded USB events (COM number may change), run verified semantic `IDENTIFY` on the *same qualified target* and require chip/board binding evidence + exact expected `buildId`, `protocolVersion`, target and mandatory capabilities, fresh `bootId`. Do not report READY if firmware booted but wrong build, the USB device changed, or a stale identity was cached. No automatic START or implicit scenario deployment after update.

**Stage failure rule:** First terminal failure wins, later stale callbacks ignored by generation. A single diagnostic record contains stage, machine error code, revision/profile, expected/observed identifiers and limited bounded logs (no secrets, no automatic upload). When failed, release the flasher/port, stop all associated timers, and offer Retry/Recover based on observed state; no infinite reconnect. Abort on power/USB loss, port ownership failure, unknown board profile, security/download-mode restrictions, or integrity mismatch.

## D. Bounded timing and resource budget

Existing timeouts are baselines, not license to make indefinite progress: flasher launch 15 s, ROM probe 30 s, write 180 s, reset 20 s. Proposed post-reset USB+identity deadline 45 s, serial handoff bounded by existing watchdog, **whole authorized transaction ≤300 s** from start to terminal outcome, measured with monotonic elapsed time. Stage deadlines and absolute deadline cannot be extended by progress output or repeated USB events. Retry is a new explicit attempt, not background looping. At most one controlled re-enumeration sequence with bounded attempts and a cap; operator retry never inherits an old successful probe.

UI thread must stay responsive; reuse existing `FirmwareWorker` asynchronous QProcess and bounded log ring. Pure catalog selection occurs once per immutable package/device identity; no repeated JSON/hash parsing on every QML frame, no new polling at 4 kHz, no new real-time firmware task or EMAC/PTP dependency. A result is immutable per attempt. UI reports aggregate state, details panel lazy/optional.

## E. Failure matrix (mandatory negative tests)

- v1.3 selects v3.x / v3.2 selects legacy -> reject pre-write.
- ROM reply malformed/empty/partial; chip != ESP32-P4; unknown v4.x; unqualified minor; ambiguous supported-board identity -> reject.
- Corrupt image, manifest tampering, duplicate/overlapping selector, duplicate filename, path traversal, missing binary/flasher, image header vs manifest disagreement -> reject before release/write.
- A preinstalled v1 manifest and M3.H.3 asset remain readable for historical pre-v3 only, never advertised v3-capable.
- User tries update while SV or PTP RUNNING; STOP rejected, timeout, unknown running state -> do not enter flash.
- USB disappears during port release, ROM probe, before write, mid-write, reset or postboot; stale `sessionGeneration`, old-boot `IDENTIFY`, two boards swap COM numbers -> no cross-device flash and no false success.
- Two concurrent update clicks; late QProcess callback; busy COM/access denied; flasher missing/AV delayed; flasher output truncated; progress moves backwards; flasher hangs after reporting 100% -> one bounded terminal result.
- `write-bin` returns success but firmware crashes, bootloops, wrong build/capabilities/target, USB returns different physical board -> not Ready.
- Unknown secure-download/flash-encryption state -> fail closed with support diagnosis, never `--force` or unsafe workaround.
- Repeated optional update, failed attempt followed by explicit recovery, already-current firmware -> no stale state or accidental board reset.
- Legacy M3.H.3 running autonomy + M4 3-MU draft/roster/reconnect and priority STOP remain unaffected.

Use existing `arstack_session_harness`, `--check-firmware-contract` / Qt packaging checks, firmware CI and Linux/Windows installer lifecycle tests. Add deterministic pure catalog/ROM-parser fixtures in the current test targets instead of separate standalone background services/jobs.

## F. CI, hardware gates, release protocol

**CI-efficient execution:** One scoped source commit/PR checkpoint; existing `esp32p4-smv-injector.yml` builds both profiles only when firmware/packaging gate is requested. Existing Windows Native Preview + Qt/packaging and Linux AppImage reuse resulting immutable per-profile artifact. Do not add a global every-push matrix or parallel redundant firmware rebuild. Keep exact-source check, checked-in policy, generated config, image header, merged image, catalog and SHA bound together. Cache only compiler objects separated by profile/toolchain, never reused sdkconfig or firmware image across revisions.

**HIL-A v1.3:** pre-v3 first boot/update/recovery, USB mapping, SV+PTP, standalone autonomy, repeatable STOP and no regression to original measured M3.H.3 wire profile. **HIL-B v3.2:** Waveshare ESP32-P4-ETH-M physical ROM probe using shipped Windows/Linux tool, flash, fresh boot, Ethernet PHY, canonical 4I+4V 4000 SV fps, no missing/reordered/duplicate counters, PTP Layer-2 frames and truthful `smpSynch`, cold boot / reset / reconnect, interrupted-flash recovery. Record serial ROM identity, exact firmware+flasher+IDF version, sha, board model, capture and error traces. CI green is not HIL PASS.

**Release gating:** v3.x engineering preview only after CI artifacts + at least boot/reconnect/flasher HIL-B; stable promotion only after both hardware acceptance lanes and honest packet timing evidence. Mark support by **qualified board model + silicon revision**. Never overwrite immutable M3.H.3 tag/assets. Other active M4 PRs and independent LAN autonomy PR must be audited for overlap, not rewritten in this milestone.

## G. Implementation order (one coherent milestone, avoid patch stacking)

1. Baseline current `main` / active M4 PRs / existing boot and installer fixtures; freeze v1 stable.
2. Implement pure v2 catalog/selection + revision/parser/image-header tests, with v1 strict compatibility.
3. Compile independent pre-v3 and v3.x firmware lanes in isolated generated-config/output locations; prove header/manifest and pinned flasher CLI + board support.
4. Extend existing `FirmwareManager` and `SmartSessionController` transaction for one-authority guarded selection, RUNNING/unknown-state safety, deadlines and reset/postboot proof.
5. Wire compact UX with no extra screens and one primary operator action; details remain optional. Add full negative session regressions and packaged-offline tests.
6. Physically test v1.3 and v3.2; release separate v3.x preview; promote only with evidence.

### Primary vendor and security references

- https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32p4/api-reference/system/chip_revision.html
- https://documentation.espressif.com/en/PCN202600801_ESP32-P4_Chip_Revision_v3.2_Upgrade_Chip_Revision_v1.3_Demand_Collection_and_EOL_Plan_Description.html
- https://documentation.espressif.com/AR2026-004_Compatibility_Advisory_for_ESP32-P4_Chip_Revision_v3.2_EN.html
- https://github.com/waveshareteam/ESP32-P4-Platform/blob/main/docs/ESP32P4_REVISION_CONFIG.md
- https://github.com/esp-rs/espflash/releases/tag/v4.5.0
- https://docs.espressif.com/projects/esptool/en/latest/esp32p4/esptool/basic-commands.html
- https://www.ncsc.gov.uk/collection/device-security-principles-for-manufacturers/provide-updates-securely
