# SCL Import / Export — Start Here

For any change involving **Open SCL**, **Save/Export SCL**, live-model reconstruction, IED-name inference, SCL type synthesis, Edition 2/Edition 1 conversion, IID/ICD output, or SCL-assisted online validation, read:

- **[`docs/SCL_IMPORT_NORMALIZATION_PROFILE.md`](docs/SCL_IMPORT_NORMALIZATION_PROFILE.md)** — Open SCL -> normalize -> canonical model -> multi-edition export.
- **[`docs/SCL_EXPORT_RECONSTRUCTION_PROFILE.md`](docs/SCL_EXPORT_RECONSTRUCTION_PROFILE.md)** — live MMS discovery -> canonical model -> multi-edition export.

Thirty-second contract:

```text
                    LIVE MMS DISCOVERY
                           |
                           v
                    typed live evidence
                           |
                           +-------------------+
                                               |
                                               v
                                      CANONICAL IED MODEL
                                               ^
                                               |
                           +-------------------+
                           |
                           v
                    SCL IMPORT/NORMALIZE
                           ^
                           |
                        OPEN SCL

CANONICAL IED MODEL
  identity + communication
  LD/LN/DO/DA/type semantics
  declared DataSets/RCBs/configuration
  optional live runtime overlay
  provenance + diagnostics
       |
       +--> Edition 2 Schema V3.1 -> IID
       +--> Edition 1 Schema V1.6 -> ICD
       +--> Edition 1 Schema V1.5 -> ICD
       `--> Edition 1 Schema V1.4 -> ICD
```

Do not forget:

- **One semantic model, multiple ingress paths.** Live discovery and Open SCL must converge on the same canonical IEC 61850 semantics before export.
- Save/export is a **local projection**. Selecting another schema must not rediscover the IED or require a live connection.
- Open SCL is a **semantic import**, not XML-DOM retention plus string replacement.
- `IED@name` is authoritative for the opened file; live-discovery identity uses evidence scoring. On SCL-assisted connect, cross-check them and expose mismatches instead of silently renaming.
- Declared SCL configuration is not proof of current live runtime state. Keep structural/configuration evidence separate from live overlays such as RCB ownership, EntryID, current values, or runtime-added DataSets.
- Edition compatibility belongs in typed import/export profiles. Unknown or unrepresentable semantics are not silently converted to `false` or defaults.
- Same-edition round-trip acceptance is **semantic idempotence**, not byte-for-byte XML equality.
- Source-only information such as original type IDs, descriptions, topology, Header/history and private extensions belongs in source provenance/evidence; it must not become a second canonical model.
- IED-name resolution for live discovery is an **evidence-scored semantic resolver**, not a worker-thread trick and not a blind `longestCommonPrefix()` call.
- A live export represents the **current discovered MMS model**, including currently visible DataSets and RCB bindings when discovered.
- MMS cannot recover every engineering-only detail from the original SCL. Synthetic deterministic type IDs and explicit provenance are required for discovery-derived models.
- Capture-specific counts, names, timings and lexical quirks are evidence, not universal IEC 61850 constants.

Related knowledge:

- [`docs/MMS_DISCOVERY_WIRE_PROFILE.md`](docs/MMS_DISCOVERY_WIRE_PROFILE.md)
- [`docs/ONLINE_MODEL_CONNECT_DECISION.md`](docs/ONLINE_MODEL_CONNECT_DECISION.md)
- [`docs/SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](docs/SCL_ASSISTED_MMS_CONNECT_PROFILE.md)
- [`RCB_REPORTING.md`](RCB_REPORTING.md)
