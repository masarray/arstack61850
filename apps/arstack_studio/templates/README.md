# ARStack Studio reference templates

The first public-release target intentionally ships one built-in Sampled Values reference profile:

- **4 current + 4 voltage** (`Ia, Ib, Ic, In, Ua, Ub, Uc, Un`)
- `INT32 value + Quality` for each channel
- 16 ordered FCDA leaves / 64-byte sample payload
- one ASDU
- 4000 samples/s (`SmpPerSec`)
- ARStack-owned `svID`, IED and engineering identifiers
- multicast destination `01-0C-CD-04-00-01`
- APPID `0x4000`
- VLAN VID 0 / PCP 4
- configuration revision 1

`arstack_4i4v_9-2le_reference.scd` is a convenience engineering source, not a second runtime profile implementation. ARStack Studio loads it through the same `SclParser -> SvPublisherProfileCompiler -> ESP32-P4 support classifier` path used for external SCL/CID/SCD/IID files.

## Counter policy

Generic SCL does not universally prove the `smpCnt` wrap policy. The bundled ARStack reference profile is different: its product contract explicitly defines a 4000-count cycle (`0..3999`). `SclProfileModel::loadReferenceTemplate()` therefore supplies modulus 4000 as trusted application-owned profile context so the reference stream resolves to Class A.

This exception is **only** for the bundled reference profile. External engineering files retain the normal Class A/B/C behavior and require explicit confirmation when their counter policy is not independently established.

## Release boundary

The current ESP32-P4 firmware deployment bridge remains intentionally limited to this physically proven 4I+4V wire layout. Adding more reference templates must not imply firmware support for additional layouts; each future template requires parser/compiler regression, firmware capacity/support classification, and retained on-wire validation.

The reference template is an engineering/test convenience and is not a formal IEC 61850 conformance or certification claim.
