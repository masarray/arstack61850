# Online Model Connect Invariants

These invariants exist to prevent future implementation drift.

## Path selection

- No trusted local SCL model: use live MMS discovery.
- Trusted CID/SCL model available: use SCL-assisted connect.
- Both paths converge on the same canonical initial FC-root snapshot planner.

## Full live discovery

- Start with `GetNameList(Domain, VMD)`.
- Enumerate per-domain `NamedVariable` names with bounded `continueAfter` pagination.
- Probe Logical Node roots with `GetVariableAccessAttributes` rather than every DA leaf.
- Use selected Reads as semantic evidence where needed.
- Discover named-variable lists/DataSets when enabled.
- Build the canonical model before the shared initial snapshot stage.

## SCL-assisted connect

- Parse and select the IED/AccessPoint locally.
- Resolve endpoint and association context from SCL where available.
- Associate, then validate online MMS domains.
- Do not repeat full NamedVariable discovery by default.
- Do not repeat full GVAA/type discovery by default.
- Do not rebuild DataSets from MMS during minimum initial connect by default.
- Build FC-root Reads from the SCL-derived canonical model.

## Shared initial snapshot

- Read only FC roots that actually exist for each LN.
- Preserve deterministic request ordering.
- Compatibility default: no more than 10 variable references per initial Read.
- Keep the per-Read reference bound separate from the negotiated outstanding-service limit.
- Compatibility default: one confirmed initial Read outstanding at a time.
- Reassemble COTP DT segmentation to EOT before upper-layer decode.
- Decode nested MMS Data against the canonical type tree.
- Keep the MMS association established after successful synchronization.

## Safety

The minimum discovery/connect paths are read-only and must not silently add Write, control, GI, RCB reservation/enable, dynamic DataSet mutation, or file-service mutation.

## Evidence discipline

Capture-specific object counts, identifiers, timings, AP-title examples, and request totals are regression evidence, not universal IEC 61850 constants.
