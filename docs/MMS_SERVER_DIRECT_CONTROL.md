# MMS Server Direct-Normal Control Trial

This slice advances the clean MMS server prototype from read-only browse/read to a bounded, explicitly writable IEC 61850 control target.

## Implemented server model

Domain: `ESP32S3IOLD0`

Logical Nodes:

- `LLN0`
- `LPHD1`
- `GGIO1`

Read-only digital inputs:

- `GGIO1.Ind1` .. `GGIO1.Ind8`

Direct-with-normal-security Boolean controls:

- `ESP32S3IOLD0/GGIO1.SPCSO1`
- `ESP32S3IOLD0/GGIO1.SPCSO2`
- ...
- `ESP32S3IOLD0/GGIO1.SPCSO8`

Each control exposes:

- `ST$SPCSO<n>$stVal`
- `CF$SPCSO<n>$ctlModel` = `1` (Direct-with-normal-security)
- `CO$SPCSO<n>$Oper`

The live `Oper` TypeSpecification is the bounded SPC shape:

1. `ctlVal` Boolean
2. `origin` structure (`orCat` Unsigned, `orIdent` OctetString)
3. `ctlNum` Unsigned
4. `T` UTC time
5. `Test` Boolean
6. `Check` 2-bit BitString

A static non-deletable DataSet is also exposed:

`ESP32S3IOLD0/LLN0.Outputs`

It contains the eight `SPCSO<n>.stVal` status leaves and is intended to become the first static reporting DataSet in the next server-side RCB tranche.

## Safety semantics

The `Oper` decoder is allocation-free and rejects malformed or unexpected structures.

- `ctlNum=0` is rejected.
- origin category outside the bounded IEC range is rejected.
- synchro/interlock Check requests are fail-closed by default because this prototype does not yet implement those plant checks.
- `Test=true` is accepted but does not change the live output state.
- an application/hardware apply callback can reject the command before the published status changes.
- there is no command retry.

This tranche implements only Direct-with-normal-security. SBO and enhanced-security CommandTermination belong to later server slices.

## Windows build

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\build_mms_direct_control_server.ps1
```

The script configures, builds, and runs the direct-control unit/help contract.

## Run

Use a non-privileged lab port first:

```powershell
.\build-mms-direct-control\Release\ariec61850_mms_direct_control_server.exe `
  --bind 0.0.0.0 `
  --port 8102 `
  --di-mask 0x55 `
  --do-mask 0x00
```

Expected startup marker:

```text
MMS_DIRECT_CONTROL_SERVER_READY ... controls=8 ctlModel=1 dataset=ESP32S3IOLD0/LLN0.Outputs
```

For a standards-port laboratory run, use TCP 102 only when the host privileges/firewall configuration allow it.

## IEDScout trial

Connect IEDScout as an MMS/IEC 61850 client to the host running this executable.

Browse `ESP32S3IOLD0/GGIO1` and verify that `SPCSO1..8` expose `ctlModel=1`, `Oper`, and `stVal`. Operate one Boolean output and then read its `stVal`.

After the client disconnects, the server prints a summary such as:

```text
MMS_DIRECT_CONTROL_STATE doMask=1 acceptedOps=1 rejectedOps=0
```

Retain terminal output and a PCAP/PCAPNG for interoperability evidence. This is simulator/server interoperability evidence, not IEC 61850 conformance certification.

## Current boundary / next server tranche

Still pending after this slice:

1. server-side URCB/BRCB objects and InformationReport transmission;
2. static DataSet report binding and GI/event triggers;
3. SBO normal and enhanced control plus CommandTermination/LastApplError;
4. concurrent-client ownership/contention;
5. server-side FileDirectory/FileOpen/FileRead/FileClose;
6. GPIO/lwIP adapter on the target ESP board;
7. runtime SCL/CID-driven model generation;
8. TLS/security profiles and broader multi-vendor evidence.
