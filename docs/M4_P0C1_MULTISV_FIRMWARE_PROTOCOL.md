# M4-P0C1 — ESP32-P4 Multi-SV V2 Control Plane (Not Yet the TX Engine)

**Status:** implementation on feature branch / PR under test. **Do not use M3.H.3 official firmware** for these commands; the new `MULTISV` protocol is available only in a firmware build from this change.

## Design boundary

The existing `PROFILE` command, single-stream 4I+4V SV publisher, live signal bank, autonomous board behavior, PTP task, and V1 START are **unchanged**. A new **control-plane-only** `MultiSvConfigStore` receives the M4 host compiler's `ARM3` V2 envelope and commits three validated profiles to a separate fixed-capacity RAM bank. This step **does not activate triple-stream Ethernet TX**. An operator may still use the existing single SV output through the old command set.

Firmware identifies support using `MULTISV-CONFIG-STAGING-V2`. The capability `SV-MULTI3-V1` must **not** appear until M4-P0C2 actually implements and measures three on-wire canonical SV publishers. All `MULTISV STATUS/SHOW/COMMITTED` responses contain `output=INACTIVE_UNTIL_M4_P0C2`.

## Serial command reference (192-byte ASCII command limit)

```text
MULTISV STATUS
MULTISV SHOW
MULTISV BEGIN <transaction_id_decimal> <encoded_length_decimal>
MULTISV CHUNK <transaction_id_decimal> <offset_decimal> <hex_data_max_48_bytes>
MULTISV VALIDATE <transaction_id_decimal>
MULTISV COMMIT <transaction_id_decimal>
MULTISV ABORT <transaction_id_decimal>
```

Recommended Studio flow:

1. Use **existing** `SclParser` + `M4ThreeSvRosterCompiler`: compile **exactly 3** explicit 9-2LE profiles for the provided SCD; all 3 must be supported 4I+4V, 4000 Hz, 64B, `nofASDU=1`, verified counter wrap 4000.
2. `M4ThreeSvBinaryCodec::encode_into()` writes a single `ARM3 V2` binary envelope with three existing nested `ARSV V1` records. Compute `sv_binary_profile_identity_hash` of the **entire** envelope, not three separately.
3. With SV **STOPPED**, call `BEGIN` with strictly increasing transaction ID and the exact V2 binary size. Send `CHUNK` bytes sequentially, offset zero first, no overlap/gaps/replay, max **48 bytes** per command, 96 hex characters. A malformed chunk automatically aborts the transfer.
4. `VALIDATE` only succeeds after receiving the entire correctly checksummed V2 payload and decoding **all three** profiles. It also checks shared 4000 Hz and distinct complete wire identities; the SCD's **repeated APPID 0x4000 on different MACs** is valid.
5. `COMMIT` replaces the stored control-plane roster **only after VALIDATE**. The previous committed roster is unchanged after missing/duplicate chunks, CRC error, missing VALIDATE, or failed COMMIT. `STATUS` exposes committed generation/hash/length; `SHOW` additionally prints all three MAC/svID/APPID/VLAN/rate/ASDU/configRevision entries for engineer review.
6. A standalone old single-stream `START` aborts incomplete `MULTISV` upload; the committed three-stream configuration remains inert. Closing Studio does **not** erase a committed roster in powered board RAM. A reboot resets the volatile roster and its transaction sequence.

The console control task has 7168 bytes of stack in the current ESP-IDF build. The streaming buffer is statically allocated, as are candidate and committed rosters, with **no heap allocation or XML parsing** in the MCU control path. The full V2 decode uses preexisting firmware-safe V1 records and remains on the low-priority console control task, **never ISR/publisher**.

## Files

- `embedded/esp32p4_smv_injector/main/multi_profile_staging.hpp`: reusable bounded upload + all-or-nothing V2 validated commit.
- `embedded/esp32p4_smv_injector/main/multi_profile_control.cpp`: 192-byte legacy console parsing / response / STATUS / SHOW.
- `embedded/esp32p4_smv_injector/main/live_control.cpp`: command routing, capability tagging, V1 START transfer abort.
- `embedded/esp32p4_smv_injector/components/arstack_process_bus/CMakeLists.txt`: links the shared V2 codec into firmware.
- `tests/test_m4_multi_profile_staging.cpp`: GCC/Clang/MSVC host verification of real SCD-equivalent three-stream V2 upload and rollback.

## Engineering acceptance and next gate

- C++ CI must compile and run `ariec61850_m4_multi_profile_staging_tests` for GCC/Clang/MSVC; ESP32-P4 build must compile/link the new `MULTISV` command route and shared `m4_three_sv_binary.cpp`.
- No claim of three simultaneous Ethernet SV outputs, a shared measured PTP epoch, or three independent live injection setpoints is made at this checkpoint.
- **Next — P0C2:** connect an atomic committed triple roster to `SvStreamRuntime[3]` and one EMAC/PTP-anchored GPTimer. Implement the **three packet send loop** before advertising `SV-MULTI3-V1`, then implement per-slot bank/GUI and capture the 12,000 fps target.
