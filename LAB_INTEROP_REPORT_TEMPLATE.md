# IEC 61850 Interoperability Evidence Report

## Target

- Vendor:
- Model:
- Firmware:
- IP / MMS port:
- Test date and timezone:
- Isolated engineering network:
- C++ commit:
- C# oracle commit:
- Operator:

## Phase 4C.1 read-only discovery

- Command used:
- Cycle count:
- Timeout:
- Successful cycles:
- Stable fingerprint: yes / no
- Fingerprint:
- Logical Devices:
- Logical Nodes:
- Data Objects:
- Data Attributes:
- Exact MMS types:
- DataSets:
- BRCBs / URCBs:
- Warning count:
- `interop-summary.json` reference:
- Packet-capture reference:

## C#↔C++ parity

- C# `live-ied-model-v1` file:
- C++ `live-ied-model-v1` file:
- Parity report:
- Blocking findings:
- Informational findings:
- Accepted: yes / no
- Exceptions and rationale:

## Robustness evidence

- Multi-page GetNameList observed: yes / no
- Timeout scenario exercised: yes / no
- Clean reconnect after timeout: yes / no
- Ten-cycle primary-vendor run: pass / fail
- Fingerprint changed after deliberate model/config change: yes / no / not tested

## Read-only safety review

Confirm the run emitted only GetNameList, GetVariableAccessAttributes,
GetNamedVariableListAttributes, and Read. Confirm no Write, GI, RCB reservation/enable,
control, dynamic DataSet mutation, or file-service request was used.

## Conclusion

- Phase 4C.1 accepted for this target: yes / no
- Remaining actions:
- Reviewer:
- Review date:
