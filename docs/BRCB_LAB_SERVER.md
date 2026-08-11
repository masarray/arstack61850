# BRCB live interoperability lab server

`ariec61850_brcb_lab_server` is a dedicated desktop IEC 61850 MMS server for
live buffered-report interoperability work. It is intentionally separate from
the read-only `ariec61850_static_ied_server` reference model.

This is a **server-side** test. Run arstack61850 on the PC, then connect an IEC
61850 client such as IEDScout to the PC. It is not a test where arstack61850
connects outward to a hardware IED server.

No IEC 61850 conformance claim is made by this tool.

## Build on Windows

```powershell
cmake -S tools/brcb_lab_server -B build-brcb-lab
cmake --build build-brcb-lab --config Release
.\build-brcb-lab\Release\ariec61850_brcb_lab_server.exe --self-test
```

Expected self-test marker:

```text
BRCB_LAB_SELF_TEST PASS
```

## Start the lab server

Use an unprivileged port for first testing:

```powershell
.\build-brcb-lab\Release\ariec61850_brcb_lab_server.exe --port 8102 --report-period-ms 1000 --max-connections 1
```

Expected startup marker includes:

```text
BRCB_LAB_SERVER_READY port=8102 domain=ESP32S3IOLD0 brcb=LLN0$BR$BRCB1 dataset=LLN0$EventData
```

Connect IEDScout to `127.0.0.1:8102` when it runs on the same PC, or to the
PC's LAN IPv4 address on port 8102 from another machine.

## Static model

Domain:

```text
ESP32S3IOLD0
```

DataSet:

```text
ESP32S3IOLD0/LLN0$EventData
```

The DataSet contains these eight Boolean members:

```text
GGIO1$ST$Ind1$stVal
GGIO1$ST$Ind2$stVal
GGIO1$ST$Ind3$stVal
GGIO1$ST$Ind4$stVal
GGIO1$ST$Ind5$stVal
GGIO1$ST$Ind6$stVal
GGIO1$ST$Ind7$stVal
GGIO1$ST$Ind8$stVal
```

Buffered report control block base:

```text
ESP32S3IOLD0/LLN0$BR$BRCB1
```

Exposed MMS BRCB attributes:

```text
LLN0$BR$BRCB1$RptID
LLN0$BR$BRCB1$RptEna
LLN0$BR$BRCB1$DatSet
LLN0$BR$BRCB1$ConfRev
LLN0$BR$BRCB1$PurgeBuf
LLN0$BR$BRCB1$EntryID
LLN0$BR$BRCB1$ResvTms
LLN0$BR$BRCB1$Owner
```

## First IEDScout test sequence

1. Connect and browse `ESP32S3IOLD0`.
2. Confirm `LLN0$EventData` is visible and its eight members can be read.
3. Read all eight BRCB attributes above before changing anything.
4. Set `RptEna=true`. If no reservation exists, the BRCB object layer claims
   the live association automatically with `ResvTms=0` semantics.
5. Optionally set `ResvTms` to a positive value before enabling when testing
   reservation persistence across disconnect/reconnect.
6. Leave the connection open. With the default `--report-period-ms 1000`, the
   server toggles `GGIO1.Ind1` once per second and captures it as a retained
   BRCB data-change event.
7. Confirm IEDScout receives InformationReport messages and that the server
   prints `BRCB_REPORT_SENT`.
8. Set `RptEna=false` before a clean disconnect when testing normal disable.

Useful server-side evidence markers are:

```text
CONNECTION_ACCEPTED
BRCB_EVENT
BRCB_CAPTURED
BRCB_REPORT_SENT
CONNECTION_CLOSED
```

`BRCB_REPORT_SENT` is emitted only after the complete InformationReport frame
has been accepted by the socket and the owner-authorized second-phase delivery
commit succeeds. A failed/partial send does not advance the retained delivery
cursor.

## Wireshark

For the default lab port:

```text
tcp.port == 8102
```

For standard IEC 61850 MMS port 102:

```text
tcp.port == 102
```

Capture at least:

- COTP CR/CC;
- ACSE AARQ/AARE;
- MMS Initiate request/response;
- BRCB Read/Write requests and responses;
- InformationReport frames;
- disconnect/connection close.

## Current scope

The lab server currently proves the bounded BRCB R1-R3 server path in a live
socket adapter: ownership, RptEna/ResvTms control, retained capture, EntryID,
replay cursor, lifecycle cleanup, negotiated outbound MMS/COTP limits, and
owner-authorized two-phase delivery.

It does not claim TLS, authentication, runtime SCL model loading, multi-client
concurrent scheduling, outbound COTP segmentation, hardware-I/O integration, or
IEC 61850 conformance certification.