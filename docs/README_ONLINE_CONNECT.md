# Online Connect Documentation Index

Use this page only as a compact navigation aid.

| Question | Read this |
|---|---|
| Which online-model path should code choose? | [`ONLINE_MODEL_CONNECT_DECISION.md`](ONLINE_MODEL_CONNECT_DECISION.md) |
| How does full live MMS model discovery behave on the wire? | [`MMS_DISCOVERY_WIRE_PROFILE.md`](MMS_DISCOVERY_WIRE_PROFILE.md) |
| How should connect behave when CID/SCL is already loaded? | [`SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](SCL_ASSISTED_MMS_CONNECT_PROFILE.md) |
| What is the current ARStack live-discovery implementation boundary? | [`../LIVE_DISCOVERY_PROFILE.md`](../LIVE_DISCOVERY_PROFILE.md) |

Core implementation rule:

```text
no trusted SCL -> discover model from MMS -> shared FC-root initial snapshot
trusted SCL    -> validate online domains  -> shared FC-root initial snapshot
```

Do not merge the two entry paths into one indiscriminate discovery routine.
