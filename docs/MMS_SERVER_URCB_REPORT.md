# MMS Server Static URCB / InformationReport Trial

This tranche adds a runnable server-side unbuffered report path on top of the bounded MMS server core.

## Model

Domain: `ESP32S3IOLD0`

Static DataSet:

`ESP32S3IOLD0/LLN0.Outputs`

Members, in fixed order:

- `GGIO1$ST$SPCSO1$stVal`
- `GGIO1$ST$SPCSO2$stVal`
- `GGIO1$ST$SPCSO3$stVal`
- `GGIO1$ST$SPCSO4$stVal`
- `GGIO1$ST$SPCSO5$stVal`
- `GGIO1$ST$SPCSO6$stVal`
- `GGIO1$ST$SPCSO7$stVal`
- `GGIO1$ST$SPCSO8$stVal`

URCB:

`ESP32S3IOLD0/LLN0$RP$Outputs01`

Default `RptID`: `ARSTACK-Outputs01`.

The object bank exposes `RptID`, `RptEna`, `Resv`, `DatSet`, `ConfRev`, `OptFlds`, `BufTm`, `TrgOps`, `IntgPd`, `GI`, and `SqNum` as MMS variables.

## First acceptance profile

The first live gate is deliberately deterministic and GI-driven:

1. associate;
2. inspect the static DataSet and URCB;
3. reserve the URCB if the client uses `Resv`;
4. set `RptEna=true`;
5. set `GI=true`;
6. receive one MMS `InformationReport` carrying all eight ordered DataSet values;
7. verify `SqNum` advances only after a complete report frame is encoded;
8. disable/release and disconnect.

The initial `OptFlds` enable sequence number, reason, DataSet name, data references, and configuration revision. Report time is intentionally omitted in this first profile so timestamp semantics do not obscure the basic interoperability gate.

## Safety and ownership boundary

This executable currently serves one client at a time.

`RptEna` and `Resv` are explicitly reset to false at connection boundaries so one disconnected client cannot leave a live reservation/enable state for the next client.

No dynamic DataSet mutation is available on this server. `LLN0.Outputs` is static and non-deletable.

No automatic report-control takeover or retry is performed.

## Build

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\build_mms_direct_control_server.ps1
```

The helper builds/tests both server trial executables.

## Run

```powershell
.\build-mms-direct-control\Release\ariec61850_mms_urcb_server.exe `
  --bind 0.0.0.0 `
  --port 8102 `
  --mask 0x55
```

Expected startup marker:

```text
MMS_URCB_SERVER_READY ... dataset=ESP32S3IOLD0/LLN0.Outputs urcb=ESP32S3IOLD0/LLN0$RP$Outputs01 members=8 ...
```

On each emitted report the server prints:

```text
MMS_URCB_REPORT_SENT index=0 sqNum=<n> reason=8 bytes=<n>
```

Reason `8` is General Interrogation.

For TCP 102, use a laboratory host configuration that permits binding the standard MMS port.

## IEDScout trial

Connect IEDScout to the server host and browse `ESP32S3IOLD0`.

Verify the `LLN0.Outputs` DataSet and `LLN0$RP$Outputs01` URCB are visible. Enable the report and issue GI. The expected evidence is one report with eight Boolean values matching the configured `--mask` bit order.

Retain console output and PCAP/PCAPNG. Until this live run is performed, this tranche is classified as software/offline-tested rather than live-proven server interoperability.

## Still pending

- event-on-change/data-change triggering;
- multiple URCBs and concurrent-client ownership;
- BRCB retention/replay/EntryID/PurgeBuf on the runnable server adapter;
- Direct/SBO enhanced server control and CommandTermination;
- server-side MMS file services;
- GPIO/lwIP hardware adapter;
- runtime SCL/CID model generation;
- TLS/security profiles and broader multi-vendor evidence.
