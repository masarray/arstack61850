# Static IEC 61850 server foundation

`ariec61850_static_ied_server` is the first runnable desktop adapter for the
bounded MMS server core. It deliberately keeps platform networking outside the
protocol engine, matching the same boundary required by ESP-IDF/lwIP.

```text
TCP listen/accept (WinSock or BSD sockets)
    -> connected TcpByteStream callbacks
    -> MmsStaticServerSession fixed-buffer pump
    -> MmsStaticConnectionRuntime
    -> MmsStaticApplicationDispatcher
    -> static object/DataSet tables
```

The included desktop reference model is read-only:

```text
ESP32S3IOLD0
|- LLN0  Mod.stVal
|- LPHD1 PhyHealth.stVal
`- GGIO1 Ind1.stVal ... Ind8.stVal
```

## Desktop smoke run

Build with `ARIEC61850_BUILD_TOOLS=ON`, then run an unprivileged development
endpoint such as port 8102:

```powershell
.\ariec61850_static_ied_server.exe --port 8102 --digital-input-mask 0x35 --max-connections 1
```

The executable default is TCP port **102**, the standard IEC 61850 MMS port.
Use `--port 8102` when local permissions or another process prevent binding
port 102. `--digital-input-mask` defaults to `0x35`; `--max-connections 0`
means unlimited sequential accepts.

Implemented server services in this slice:

- COTP connection accept with negotiated TPDU-size bounds;
- ACSE association accept and MMS Initiate response with the peer MMS PDU limit
  retained and enforced on outbound confirmed responses;
- `GetNameList`;
- `GetVariableAccessAttributes`;
- `GetNamedVariableListAttributes` when a static DataSet table is supplied;
- `Read`;
- bounded `Write` only for object entries that explicitly provide a write
  callback (the included desktop model provides none).

The public `ARIEC61850::mms_server_core` also contains the bounded BRCB R1-R3
runtime, ownership/lifecycle objects, retained replay/recovery, and negotiated
outbound-limit enforcement for InformationReport delivery. The current
`ariec61850_static_ied_server` reference model does **not** yet instantiate or
schedule those BRCB objects, so a desktop IEDScout browse/read pass must not be
reported as live BRCB interoperability.

## ESP32/lwIP integration boundary

The protocol core does not include an ESP-IDF header or own a listening socket.
An ESP-IDF application accepts a TCP/102 connection through lwIP, supplies
`TcpSendFn`/`TcpReceiveFn` callbacks, gives each connection its own
`MmsStaticConnectionRuntime`, `MmsStaticServerSession`, and fixed buffers, then
calls `poll_once()` from its task or event loop. Buffer capacities and the
maximum concurrent association count remain product-profile decisions.

## Current limits

- The desktop adapter serves connections sequentially. The core is per-session
  and supports platform-owned multi-client orchestration, but the adapter does
  not yet schedule multiple clients concurrently.
- Physical IEDScout browse/read acceptance is not yet recorded.
- The reference values are startup constants; a board adapter still needs to
  bind the eight GGIO values to debounced input snapshots.
- The desktop reference adapter does not yet expose BRCB reporting or control
  objects even though the server core contains those bounded primitives.
- Authentication, TLS, file services, runtime SCL model loading, and outbound
  COTP segmentation are not implemented in this reference adapter. Responses
  that cannot fit the negotiated peer TPDU/MMS limits fail closed instead of
  emitting an oversized frame.