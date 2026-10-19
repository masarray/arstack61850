# Bench live-control commands

This serial command surface validates realtime live-update semantics and the laboratory PTP/SV control path used by ARStack Studio.

```text
HELP
SHOW
START
STOP
ZERO
FREQ <millihertz>
SET <IA|IB|IC|IN|UA|UB|UC|UN> <rms_wire_counts> <phase_mdeg> [quality]
ENABLE <channel> <0|1>
QUALITY <channel> <uint32/0xhex>

PTP SHOW
PTP START
PTP STOP
PTP CONFIG <domain> <transport> <vlan> <vid> <pcp> <announce_ms> <sync_ms> <pdelay>

PROFILE SHOW
PROFILE SMPSYNCH <AUTO|0|1|2>
```

Examples:

```text
SET IA 1000 0
SET IB 1000 -120000
SET IC 1000 120000
SET UA 5774 0
SET UB 5774 -120000
SET UC 5774 120000
FREQ 50000
START

# while RUNNING
SET IA 5000 0
SET UC 10000 90000
FREQ 49950
QUALITY IA 0x00000000

# P1.75 live receiver-condition simulation while SV continues transmitting
PROFILE SMPSYNCH 0
PROFILE SMPSYNCH 1
PROFILE SMPSYNCH 2
PROFILE SMPSYNCH AUTO

STOP
```

`rms_wire_counts` is intentionally profile-neutral. Engineering-unit conversion belongs to the resolved host profile. For the current development stream, the existing bench mapping is 1 mA/count for current values and 10 mV/count for voltage values.

Live signal updates are copied into a coherent generation and become visible to the realtime publisher at a sample boundary. They do not modify stream identity, reset `smpCnt`, or rebuild BER. `START` requests a clean phase/sample-counter restart; stream identity/layout profile changes still require STOP.

`PROFILE SMPSYNCH` is deliberately different: it is a laboratory wire stimulus and may change while RUNNING. `0`, `1`, and `2` force the corresponding IEC 61850 SV `smpSynch` value and are reported as `LAB_OVERRIDE` / simulated. `AUTO` is conservative and advertises 0 until a future PTP-P2 clock-discipline engine supplies measured lock evidence. PTP packet visibility alone never promotes AUTO.

## Transactional 4I+4V scenario (SCENARIO-TX-V1)

A compatible Studio must bind a START to a fully validated firmware commit. The
firmware's single control task owns a bounded staging object; the actual publisher
continues to read only `SvLiveSignalBank`. Send only while STOPPED:

1. `SCENARIO BEGIN <uint32-token>` — initialize a fresh staging epoch.
2. `FREQ <millihertz>`, all eight `SET <id> <counts> <mdeg> <quality>`,
   all eight `ENABLE <id> <0|1>`, and `SHAPE CT ...` — these modify staging,
   not the transmitted/live bank. The transaction requires every field.
3. `SCENARIO COMMIT <token> <16-hex-digit-fingerprint>` — rejects wrong token,
   missing fields, malformed/out-of-range values, mismatch, or concurrent bank
   generation. A successful commit publishes the full bank in one generation.
4. Wait for `SCENARIO COMMIT transaction=<id> applied generation=<n> hash=<h>`.
   Request `SCENARIO SHOW <token>`, and match `SCENARIO READBACK
   transaction=<id> generation=<n> hash=<h>`.
5. Only after verified readback send `SCENARIO START <token> <hash>`.
   Firmware refuses START unless the current live bank still matches the
   committed token, hash and generation.
6. `STOP` or `SCENARIO ABORT <token>` invalidates staging/proof. Ordinary live
   edits invalidate the committed token; the worker can cancel queued START.

The fingerprint is versioned 64-bit FNV-1a over little-endian semantic fields
in `live_scenario_transaction.hpp`, not a secret/authentication primitive.
This protocol is an application-level integrity contract, not a substitute for
independently decoding the Ethernet SV payload or testing USB disconnects.
