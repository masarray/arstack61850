# Online Model Connect Implementation Checklist

Use this checklist before changing connection/discovery code.

- [ ] Determine whether a trusted CID/SCL model is already selected.
- [ ] If no SCL is trusted, use full live MMS discovery.
- [ ] If SCL is trusted, do not rediscover the whole model by default.
- [ ] In SCL-assisted mode, resolve endpoint/association context from SCL where available.
- [ ] Validate online MMS domains before bulk initial Reads.
- [ ] Reuse the canonical `InitialFcReadPlanner` concept for both entry paths.
- [ ] Enumerate only FC roots that actually exist for each LN.
- [ ] Keep the initial Read batch bound independent from outstanding-service negotiation.
- [ ] Use a compatibility default of at most 10 variable references per initial Read.
- [ ] Keep initial confirmed Reads sequential by default.
- [ ] Reassemble COTP DT segments through EOT before MMS decode.
- [ ] Map nested MMS Data through the canonical type tree.
- [ ] Keep the association open after successful initial synchronization.
- [ ] Do not silently introduce Write/control/GI/RCB mutation into minimum connect/discovery.
- [ ] Treat capture-specific numeric values as evidence, not universal constants.
- [ ] Preserve existing tested discovery behavior when introducing a new scheduler/path.

Detailed evidence:

- [`ONLINE_MODEL_CONNECT_DECISION.md`](ONLINE_MODEL_CONNECT_DECISION.md)
- [`MMS_DISCOVERY_WIRE_PROFILE.md`](MMS_DISCOVERY_WIRE_PROFILE.md)
- [`SCL_ASSISTED_MMS_CONNECT_PROFILE.md`](SCL_ASSISTED_MMS_CONNECT_PROFILE.md)
