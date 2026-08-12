# PTP-P2 Bench Acceptance Plan

This plan turns PTP-P2 from software-complete timing logic into repeatable physical evidence on ESP32-P4 and a real IEC 61850 relay.

It is deliberately stricter than “PTP packets are visible.” Passing this plan does not by itself create a certification claim; it establishes reproducible laboratory evidence for the tested topology, firmware, relay and PTP source.

## Equipment

Minimum:

- ARStack ESP32-P4 target with the intended Ethernet PHY
- real protection relay / merging-unit receiver under test
- external PTP source for TIME_RECEIVER tests
- managed or isolated process-bus Ethernet connection
- independent packet capture path where practical
- ARStack Studio / serial console for P2 telemetry

Recommended:

- reference PTP grandmaster or calibrated time source
- hardware timestamp capable capture/analyzer
- oscilloscope/time-interval counter if absolute timing is being evaluated

Record firmware commit, ESP-IDF version, relay model/firmware, PTP source model/profile and network topology with every evidence set.

## Evidence to retain

For each run retain:

1. serial/Studio PTP2 telemetry
2. packet capture containing Announce, Sync, Follow_Up and peer-delay traffic
3. selected sourcePortIdentity and grandmasterIdentity
4. domain and transportSpecific
5. t1/t2/t3/t4 evidence when available
6. meanPathDelay, offsetFromMaster, path-delay jitter and frequency correction
7. discipline transitions with timestamps
8. actual SV frame `smpSynch` values
9. relay PTP state and relay SV acceptance/rejection behavior
10. exact test configuration and pass/fail notes

## A. Cold-boot epoch acquisition

Purpose: prove a receiver can start with an uninitialized/1970-like local IEEE1588 clock and acquire the external PTP epoch without falsely entering permanent FAULT.

Procedure:

1. power-cycle the ESP32-P4 from a true cold state
2. select `TIME_RECEIVER`
3. set `PROFILE SMPSYNCH AUTO`
4. start PTP with the external source already active
5. capture the first qualified Sync/Follow_Up and peer-delay exchange
6. confirm the first very large epoch offset causes bounded acquisition phase correction
7. confirm subsequent measurements converge into the normal offset range
8. confirm state progresses `UNLOCKED -> ACQUIRING -> LOCKED`

Pass criteria:

- no permanent FAULT caused solely by the initial epoch difference
- no `smpSynch=1/2` before measured lock
- post-acquisition offsets enter the configured normal range
- later huge post-lock offsets remain subject to the normal fault/unlock policy

## B. Peer-delay correctness

Purpose: prove the receiver uses hardware timestamp evidence rather than software receive/send time.

Verify:

- t1 = local Pdelay_Req hardware TX timestamp
- t2 = peer request-receive timestamp from Pdelay_Resp
- t3 = peer response-transmit timestamp from Pdelay_Resp_Follow_Up
- t4 = local Pdelay_Resp hardware RX timestamp

Expected calculation:

```text
meanPathDelay = ((t2 - t1) + (t4 - t3) - correctionFields) / 2
```

Pass criteria:

- sequence IDs correlate correctly
- requester port identity matches the local receiver
- path delay is non-negative and within the configured limit
- repeated path delay stays within the configured jitter bound
- stale peer-delay evidence expires and is not reused indefinitely

## C. Two-step Sync / Follow_Up correlation

Purpose: prove offset uses the exact origin timestamp from the matching Follow_Up.

Expected calculation:

```text
offsetFromMaster = localSyncRx - masterOrigin - meanPathDelay - correctionFields
```

Pass criteria:

- only matching source + sequence pairs complete a measurement
- missing/mismatched Follow_Up does not create a valid measurement
- correctionField values are applied
- stale Announce or stale peer-delay evidence prevents a new qualified offset measurement

## D. Source selection and Announce lifetime

Run at least two Announce sources with different advertised quality.

Pass criteria:

- bounded source preference selects the better eligible source
- a source change clears pending Sync/Pdelay correlation and restarts acquisition
- Sync/Follow_Up traffic alone cannot keep expired Announce evidence alive
- expired source evidence removes global provenance immediately

This is a bounded lab selection policy, not a claim of full BMCA conformance.

## E. LOCKED qualification

Use the configured default thresholds first, then repeat with intentionally tighter values.

Pass criteria:

- LOCKED requires the configured number of consecutive qualified measurements
- excessive path delay or jitter is rejected
- rejected measurements do not refresh the lock-age timer
- frequency correction remains bounded
- large acquisition phase correction is not available as an unrestricted post-lock behavior

## F. AUTO `smpSynch`

Verify actual transmitted SV frames, not only the UI label.

Expected mapping:

| Measured state | Expected AUTO wire value |
| --- | ---: |
| UNLOCKED | 0 |
| ACQUIRING | 0 |
| LOCKED, no strict global provenance | 1 |
| LOCKED, strict global provenance | 2 |
| bounded HOLDOVER | 1 |
| FAULT | 0 |

Pass criteria:

- packet capture agrees with Studio/serial provenance
- PTP visibility alone never promotes AUTO
- same-source Announce degradation immediately removes global `2` eligibility
- re-promotion to `2` requires fresh global Announce evidence plus a subsequent qualified timing measurement

## G. Forced laboratory `smpSynch`

While SV is transmitting, exercise:

```text
PROFILE SMPSYNCH 0
PROFILE SMPSYNCH 1
PROFILE SMPSYNCH 2
PROFILE SMPSYNCH AUTO
```

Pass criteria:

- wire value changes live without resetting `smpCnt`
- waveform, stream identity and PTP task remain undisturbed
- Studio labels forced states as simulated/lab override
- the relay reaction to each state is captured

## H. Source loss and HOLDOVER

While LOCKED, disconnect or stop the external PTP source.

Pass criteria:

- no new invalid traffic extends valid timing evidence
- global provenance is revoked immediately when its evidence expires
- discipline enters HOLDOVER after the configured Sync timeout
- AUTO advertises local `1` only during the bounded HOLDOVER policy
- after holdover expiry, state returns to UNLOCKED and AUTO wire value returns to `0`

Reconnect the source and verify a fresh acquisition sequence is required.

## I. Negative profile tests

Repeat with intentional mismatch:

- wrong domain
- wrong transportSpecific
- wrong VLAN where applicable
- stale/missing Announce
- missing Pdelay Follow_Up
- mismatched Sync/Follow_Up sequence
- excessive peer delay
- excessive path-delay jitter

Pass criteria:

- no false LOCKED state
- no false measured `smpSynch=2`
- diagnostics identify the missing/invalid evidence class

## J. MONITOR role

Select `MONITOR` and observe an active PTP network.

Pass criteria:

- no PTP frames are transmitted by ARStack
- no discipline frequency/phase command is applied
- no measured AUTO synchronization promotion occurs
- passive counters/source observations remain available

## K. Relay acceptance record

For each relay model create a small result record containing:

- relay model and firmware
- expected PTP profile
- ARStack commit and firmware build
- external source identity/profile
- network topology
- time to ACQUIRING / LOCKED
- stable offset/path/jitter statistics
- `smpSynch` behavior
- relay SV acceptance behavior
- all deviations and workarounds

Only after this physical evidence should compatibility claims be made for that relay/profile combination.
