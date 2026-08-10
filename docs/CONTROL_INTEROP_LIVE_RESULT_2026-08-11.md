# Smart Control Live Result — 2026-08-11

## Scope and target

Phase 4D-C5 was exercised against `192.168.1.10:102`. The endpoint was verified locally before any Write:

- listener process: OMICRON IEDScout 4 (`IEDScout.exe`);
- network path: local `KM_Test` address with traffic captured through Npcap loopback;
- control object: `BCU7SLCTRL/CSWI1.Pos`;
- association profile: `BalancedApTitle`;
- automatic command retry: disabled.

This is simulator interoperability evidence, not physical-plant or IEC 61850 conformance evidence.

## Results

| Case | Live model | Result | Wire evidence |
|---|---|---|---|
| Read-only inventory | 4 domains, 5,567 variables, 80 candidates, 17 control-ready | Passed; zero control Write | `mmsControlWriteCount=0` |
| Read-only descriptor | SBO enhanced, Boolean `ctlVal`, exact `Oper`/`SBOw`/`Cancel` types | Passed; status `0x0680`; zero control Write | PCAP: 0 Write requests |
| Negative SBOw with `Test=true` | SBO enhanced | Correctly rejected and decoded as MMS DataAccessError 3 `object-access-denied`, ControlError 3 `operator-test`, AddCause 8 `blocked-by-mode` | Exactly one `SBOw`; one `LastApplError`; no `Cancel`, `Oper`, or retry |
| SBO enhanced select/cancel with `Test=false` | SBO enhanced | Passed; `SBOw` then `Cancel`; status unchanged | Exactly two Writes in order: `SBOw`, `Cancel` |
| Direct enhanced operate OFF | Direct enhanced | Passed with positive CommandTermination; status `0x0680 -> 0x0640` | Exactly one `Oper`; one InformationReport; no retry |
| Direct enhanced operate ON restore | Direct enhanced | Passed with positive CommandTermination; status restored `0x0640 -> 0x0680` | Exactly one `Oper`; one InformationReport; no retry |

The status values are MMS two-bit bit strings represented by the harness as raw bytes: `0x0680` means six unused bits with payload `10`, while `0x0640` carries payload `01`.

The live `ctlModel` was rediscovered before every action. IEDScout exposed SBO enhanced (`4`) for the selection cases and Direct enhanced (`3`) for the later operate cases. A mismatched `select-operate` request was rejected locally before any Write when the rediscovered model was Direct enhanced.

## Live-discovered interoperability repair

The negative path exposed two IEDScout `LastApplError` variants:

1. VMD-specific `LastApplError` with omitted `ctlObj`;
2. an embedded exact CO object root such as `BCU7SLCTRL/CSWI1$CO$Pos`, without an `Oper` leaf.

The decoder now accepts the omitted-`ctlObj` variant only when origin category, origin identifier, and `ctlNum` exactly match the active command. It also recognizes an exact embedded CO object root. Mismatched sequence correlation remains rejected, so an unrelated generic application error cannot complete or diagnose another command.

The C5 JSON evidence now records both the MMS DataAccessError numeric code/name and the IEC 61850 ControlError/AddCause diagnostics.

## Artifact hashes

Generated artifacts remain under the ignored local build evidence directory and are not committed to the public source tree.

| Artifact | SHA-256 |
|---|---|
| `control-inventory.json` | `A57A97C9D330FA8F33078A3617E5EBD3D450EF35A32FDB3EE6A9B7F3B4AEE41C` |
| `cswi1-pos-discovery-loopback-captured.json` | `9DBF4BDF020F3E4CCE9C0A985A33C64217EBBEFBE494F67153A2570DD80A32D9` |
| `capture-discovery-loopback.pcapng` | `10449524587E10D41152C8DE578DCB716DE65AA6BA2D9FCAC39A051109529279` |
| `cswi1-pos-sbo-enhanced-select-cancel-accepted-diagnostics.json` | `2B7A2097BC4785553A221ED9B779ACDF6E8D07DA802E3D61915E2010CF44F907` |
| `cswi1-pos-sbo-enhanced-select-cancel-accepted-diagnostics.pcapng` | `6F86D9A7F4086DB818DEF597B15D034E22BE514E9F1EBE6A119386AA9405DCF1` |
| `cswi1-pos-sbo-enhanced-select-cancel-success.json` | `04D4EEDE0A1521DAAB57339D9F3157C5AEDFD007593A490A0608AC67694A0DF2` |
| `cswi1-pos-sbo-enhanced-select-cancel-success.pcapng` | `A9D4F7A2C48210F140FEE8F13C23C26D05A4AA4CE7E6FE285D890460061886CC` |
| `cswi1-pos-direct-enhanced-operate-off.json` | `1A8F92B11C95CDB698483966E08FB9E9B3F743C7D7D8197F34F0425374AF39FB` |
| `cswi1-pos-direct-enhanced-operate-off.pcapng` | `DA315C9132B2BFEE3FAFB61808506462299F67A6926F407FF4D51227BF61CC4E` |
| `cswi1-pos-direct-enhanced-operate-on-restore.json` | `BEAA47AF23BF9E2580034D1504FF3115FA68A33AED2BBD91B3D2FE2C1DC20114` |
| `cswi1-pos-direct-enhanced-operate-on-restore.pcapng` | `E794F20BE2D5A22C9AB29C42AD8A8266281956BABE52A1CB3EAE4778F6C9D0E5` |

## Remaining acceptance boundary

These bullets describe the scope of this locally retained, hash-identified capture set. A complementary parallel simulator run covering all four control models is recorded in `CONTROL_INTEROP_ACCEPTANCE.md`; its local artifact filenames are documented there, but those artifacts are not part of this checkout.

- GCC/Clang profiles must pass on the integration PR; only MSVC was available locally.
- Direct normal and SBO normal were not exposed by this tested control object during the retained cases.
- Association-loss and multi-client contention were not forced against the running IEDScout process.
- Physical IED behavior remains unclaimed.
