# IEC 61850 Control Interoperability Runbook

## Purpose

This runbook is the physical/laboratory acceptance procedure for Phase 4D-C5.
It validates the guarded C++ control path against a real IED or a vendor simulator.
It is not an IEC 61850 conformance certificate.

Sampled Values / ESP32-P4 testing is deliberately out of scope here.

## Safety boundary

Use only an isolated, non-operational laboratory IED or simulator whose controlled
output cannot operate primary plant. Verify the physical/control circuit is safe
before arming any Write.

`ariec61850_control_interop_probe` is read-only by default. A control Write is
blocked unless the exact token below is supplied:

```text
--arm IEC61850-LAB-CONTROL
```

The harness never retries a command automatically.

## Build

### Windows / MSVC

```powershell
cmake -S tools/control_interop_probe -B build-control-interop
cmake --build build-control-interop --config Release --parallel
ctest --test-dir build-control-interop -C Release --output-on-failure
```

Executable:

```text
build-control-interop\Release\ariec61850_control_interop_probe.exe
```

### Linux

```bash
cmake -S tools/control_interop_probe -B build-control-interop
cmake --build build-control-interop --parallel
ctest --test-dir build-control-interop --output-on-failure
```

Executable:

```text
build-control-interop/ariec61850_control_interop_probe
```

## 0. Offline safety gate

Run this before connecting to an IED:

```bash
ariec61850_control_interop_probe --self-test
```

Expected:

```text
CONTROL_INTEROP_SELF_TEST PASS readOnlyDefault=true armGate=true noAutoRetry=true
```

## 1. Start packet capture

Start Wireshark before connecting.

Recommended display filter after the run:

```text
tcp.port == 102 && ip.addr == <IED_IP>
```

Retain the original PCAP/PCAPNG together with the JSON evidence produced by the
harness. Keep capture timestamps synchronized with the engineering workstation.

## 2. Read-only discovery gate

Example:

```bash
ariec61850_control_interop_probe 192.168.1.10 102 \
  --object LD0/CSWI1.Pos \
  --evidence c5-discovery.json
```

This may perform MMS Read, GVAA and GetNameList, but no Write.

Accept when the output proves:

- the object root resolves as `LD/LN.DO`;
- `ctlModel` is one of Direct normal, SBO normal, Direct enhanced or SBO enhanced;
- exact live `Oper` TypeSpecification is available;
- `SBOw` TypeSpecification is available for SBO enhanced;
- `Cancel` TypeSpecification is available for an SBO model;
- the inferred CDC/control value type is credible;
- `sboTimeout` / `operTimeout` are read when exposed, otherwise bounded fallbacks are explicit;
- the best matching ST/MX status object is identified when exposed;
- evidence JSON reports `mmsControlWriteCount: 0`.

Do not proceed to Write if discovery is incomplete or vendor-specific command
fields cannot be bound conservatively.

## 3. Direct normal acceptance

Example DPC command:

```bash
ariec61850_control_interop_probe 192.168.1.10 102 \
  --object LD0/CSWI1.Pos \
  --action operate \
  --value on \
  --value-kind dpc \
  --origin ARSTACK-C5 \
  --origin-category station \
  --interlock-check on \
  --synchro-check on \
  --arm IEC61850-LAB-CONTROL \
  --evidence c5-direct-normal-on.json
```

Acceptance:

- discovered model is `direct-normal`;
- exactly one `CO/.../Oper` Write is recorded;
- no second command Write appears in the PCAP;
- MMS service completion is accepted;
- status feedback is recorded when the IED exposes a readable ST/MX value.

Repeat only as a new deliberate operator action if an opposite command is needed.
A second manual action is not an automatic retry.

## 4. SBO normal acceptance

Use explicit sequencing so Select and Oper are visible as separate phases:

```bash
ariec61850_control_interop_probe 192.168.1.10 102 \
  --object LD0/CSWI1.Pos \
  --action select-operate \
  --value on \
  --value-kind dpc \
  --arm IEC61850-LAB-CONTROL \
  --evidence c5-sbo-normal-operate.json
```

Acceptance:

- discovered model is `sbo-normal`;
- Select is an MMS Read of `SBO`;
- selection response identifies the selected object or an accepted opaque token;
- Oper uses the immutable selected sequence;
- exactly one command Write is `Oper`;
- no automatic retry appears.

Explicit Cancel test:

```bash
ariec61850_control_interop_probe 192.168.1.10 102 \
  --object LD0/CSWI1.Pos \
  --action select-cancel \
  --value on \
  --value-kind dpc \
  --arm IEC61850-LAB-CONTROL \
  --evidence c5-sbo-normal-cancel.json
```

Acceptance:

- SBO Select succeeds;
- exactly one `Cancel` Write releases the selection;
- no Oper Write is sent.

Selection timeout should be tested only with a deliberate bounded lab procedure.
Do not automate repeated Select attempts.

## 5. Direct enhanced acceptance

```bash
ariec61850_control_interop_probe 192.168.1.10 102 \
  --object LD0/CSWI1.Pos \
  --action operate \
  --value on \
  --value-kind dpc \
  --termination-timeout-ms 10000 \
  --arm IEC61850-LAB-CONTROL \
  --evidence c5-direct-enhanced.json
```

Acceptance:

- exactly one `Oper` Write is sent;
- MMS Write acceptance alone does not complete the command;
- ordinary ST/MX reports are ignored as completion;
- a correlated `CommandTermination` completes the command;
- positive termination is recorded as `positive-termination`, or a negative
  LastApplError path records ControlError/AddCause without a retry;
- lack of termination becomes a control timeout without automatically faulting a
  healthy MMS association.

## 6. SBO enhanced acceptance

```bash
ariec61850_control_interop_probe 192.168.1.10 102 \
  --object LD0/CSWI1.Pos \
  --action select-operate \
  --value on \
  --value-kind dpc \
  --termination-timeout-ms 10000 \
  --arm IEC61850-LAB-CONTROL \
  --evidence c5-sbo-enhanced.json
```

Acceptance:

- one `SBOw` Write establishes the value-bearing selection;
- the asynchronous application-error grace window is observed;
- one `Oper` Write follows only after successful selection;
- correlated CommandTermination is required for completion;
- JSON/PCAP therefore show two deliberate command-service Writes (`SBOw`, `Oper`),
  not duplicate retries of either service.

Explicit Cancel test uses `--action select-cancel`. For SBO enhanced the expected
write sequence is `SBOw`, then `Cancel`.

## 7. Negative-path evidence

Capture at least one negative enhanced-security result when it can be produced
safely by the lab IED/simulator. Preferred cases are a simulator-configured
interlock or synchrocheck rejection.

Acceptance evidence should preserve:

- raw ControlError;
- raw AddCause;
- mapped AddCause text such as `blocked-by-interlocking` or
  `blocked-by-synchrocheck`;
- the exact control object;
- the number and identity of Write services;
- proof that no retry follows the rejection.

Do not deliberately defeat a real interlock merely to create a negative test.

## 8. Association-loss evidence

Use a simulator or otherwise safe lab setup. Establish an SBO selection, then
cause the MMS association to disappear before Oper. Verify the client fails
closed and the next command requires a new explicit action/selection.

For enhanced control, separately exercise association loss while waiting for
CommandTermination where possible.

## Evidence bundle

For each accepted case retain:

```text
C5_<IED>_<object>_<case>/
  evidence.json
  capture.pcapng
  notes.txt
  optional_ied_event_log.txt
```

Minimum `notes.txt` content:

```text
IED vendor/model/firmware:
IED IP:
Control object:
ctlModel:
Test case:
Expected safe plant/simulator response:
Observed response:
PC clock/time source:
Operator:
Date/time:
```

The JSON field `mmsControlWriteCount` and `mmsControlWrites` must agree with the
PCAP. A mismatch is a failed evidence gate and must be investigated before any
production-control claim.

## Acceptance state

Hosted CI can mark the harness `SOFTWARE_READY_FOR_LAB`.

Only retained physical/simulator evidence for the required model(s) can move the
specific tested IED profile to `LAB_INTEROP_PASSED`.

Neither state means IEC 61850 conformance certification.
