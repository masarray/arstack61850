# Online Model Connection Architecture

```text
                    +--------------------------+
                    | Canonical IED Model      |
                    +--------------------------+
                       ^                    ^
                       |                    |
              parsed CID/SCL          live MMS discovery
                       |                    |
                       +---------+----------+
                                 |
                                 v
                      InitialFcReadPlanner
                                 |
                                 v
                    InitialSnapshotRuntime
                                 |
                                 v
                              ONLINE
```

## Source-specific responsibilities

### Parsed CID/SCL path

- select IED/AccessPoint;
- resolve communication and association context where present;
- provide complete structural/type information;
- validate live MMS domain inventory;
- preserve SCL identity on mismatch and report diagnostics.

### Live MMS discovery path

- enumerate domains and variables;
- handle bounded continuation;
- obtain LN-root type evidence;
- use selected semantic Reads;
- discover DataSets when enabled;
- construct the canonical model from live evidence.

## Shared responsibilities

`InitialFcReadPlanner`:

- enumerate existing LN/FC roots;
- deterministic order;
- bounded references per Read;
- compatibility default: <= 10 references.

`InitialSnapshotRuntime`:

- sequential confirmed exchange by default;
- COTP segmentation reassembly;
- nested MMS Data mapping through canonical type information;
- per-reference diagnostics;
- persistent association after successful initial synchronization.

See [`ONLINE_MODEL_CONNECT_DECISION.md`](ONLINE_MODEL_CONNECT_DECISION.md) for the decision contract and the two detailed wire profiles for evidence.
