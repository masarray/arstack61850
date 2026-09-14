# AI / Engineer Knowledge Index — Read Before Protocol Changes

This repository contains protocol behavior that has been reconstructed from standards-facing engineering, project-owned tests, and controlled vendor-neutral black-box interoperability evidence. Do not re-derive these areas from memory or from one packet trace.

Read the topic contract before changing the corresponding code:

- **Live IP discovery vs trusted-SCL online connect:** [`docs/ONLINE_MODEL_CONNECT_DECISION.md`](docs/ONLINE_MODEL_CONNECT_DECISION.md), then [`LIVE_DISCOVERY_PROFILE.md`](LIVE_DISCOVERY_PROFILE.md).
- **RCB reporting, URCB/BRCB activation, report bit semantics, replay/recovery:** [`RCB_REPORTING.md`](RCB_REPORTING.md).
- **Live MMS model -> reconstructed SCL/IID/ICD, IED-name resolution, and Edition 2 / Edition 1 export:** [`SCL_EXPORT.md`](SCL_EXPORT.md).
- **Project engineering discipline and provenance:** [`AGENTS.md`](AGENTS.md).

For SCL export in particular, the non-negotiable architecture is:

```text
DISCOVER ONCE
    -> evidence-qualified canonical live IED model
    -> cache semantic model
    -> edition/profile transformation
    -> schema-specific serializer
```

Do **not** create separate discovery algorithms for each SCL schema. Do **not** make IED-name accuracy depend on a UI worker/thread. Do **not** use string-replacement hacks for Edition conversion. Do **not** claim the reconstructed file is the original vendor engineering file.

The detailed evidence, transformation matrix, known limits, and implementation invariants are in [`docs/SCL_EXPORT_RECONSTRUCTION_PROFILE.md`](docs/SCL_EXPORT_RECONSTRUCTION_PROFILE.md).
