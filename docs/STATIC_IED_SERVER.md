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

The included reference model is read-only:

```text
ESP32S3IOLD0
|- LLN0  Mod.stVal
|- LPHD1 PhyHealth.stVal
`- GGIO1 Ind1.stVal ... Ind8.stVal
```

## Desktop smoke run

Build with `ARIEC61850_BUILD_TOOLS=ON`, then run:

```powershell
.\ariec61850_static_ied_server.exe --bind 0.0.0.0 --port 8102 --di-mask 0x35
```

Port 8102 is the unprivileged development default. Use `--port 102` for a
normal IEC 61850 lab endpoint when the operating system permits binding it.

Implemented server services in this slice:

- COTP connection accept;
- ACSE association accept and MMS Initiate response;
- `GetNameList`;
- `GetVariableAccessAttributes`;
- `GetNamedVariableListAttributes` when a static DataSet table is supplied;
- `Read`;
- bounded `Write` only for object entries that explicitly provide a write
  callback (the included desktop model provides none).

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
- Reporting, control, authentication, TLS, file services, and runtime SCL model
  loading are outside this first read-only server slice.
