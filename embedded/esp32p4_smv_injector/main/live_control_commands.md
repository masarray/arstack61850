# Bench live-control commands

This serial command surface exists only to validate the realtime live-update semantics before the versioned host/device control protocol and desktop UI are added.

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

STOP
```

`rms_wire_counts` is intentionally profile-neutral. Engineering-unit conversion belongs to the resolved host profile. For the current development stream, the existing bench mapping is 1 mA/count for current values and 10 mV/count for voltage values.

Live updates are copied into a coherent generation and become visible to the realtime publisher at a sample boundary. They do not modify stream identity, reset `smpCnt`, rebuild BER, or claim synchronization. `START` requests a clean phase/sample-counter restart; profile identity changes require STOP/re-arm in the future compiled-profile runtime.
