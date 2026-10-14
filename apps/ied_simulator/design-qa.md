# IED Browser workflow visual QA

Date: 2026-10-14  
Result: PASS

## Scope

The Browser presentation was compared against eight user-provided commercial-tool screenshots. The implementation reproduces the useful interaction model and information hierarchy without copying vendor branding, trademarks, icons, or packaged assets.

## Verified states

- Browser shell with a compact grouped ribbon for application, endpoint, session, data, and service actions.
- Persistent dark engineering explorer with one active canonical IED context.
- Data Model hierarchy with LD, LN, DO, and DA rows.
- Dense right-pane Data Model projection with bounded direct-child rows, Back navigation, and double-click drill-down using the canonical tree.
- DataSet overview-to-member drill-down with live values resolved from the same canonical model when available.
- Reports inventory with URCB/BRCB identity, domain, logical node, selection, and contextual actions.
- RCB detail as a dense property grid, with explicit engineering/runtime evidence state.
- Received-report rows with bounded expandable value lists.
- GOOSE overview and item drill-down with control-block, addressing, VLAN, timing, and DataSet-member details.
- DataSets, Files, Setting Groups, Global Data, status history, and multi-IED tabs remain reachable.

## Intentional product differences

- ARStack identity and the existing teal/graphite design system are retained.
- Multi-IED workspace tabs remain because they are an ARStack capability.
- Unsupported or unavailable live values render as unknown (`—`); the UI does not invent false/default protocol state.
- Engineering-model GOOSE evidence remains distinct from live Ethernet capture and publication.
- Report activation remains gated by a live association; an offline SCL view does not pretend that an RCB is enabled.

## Evidence reviewed

- Side-by-side RCB detail comparison: hierarchy, grouped commands, property-grid density, report-data region, and status placement.
- Side-by-side GOOSE detail comparison: explorer drill-down, breadcrumb, attribute table, and DataSet members.
- Native rendered screenshots completed with no changed-pane QML runtime diagnostics on stderr; pre-existing diagnostics in unrelated dialogs/workspaces remain outside this change.
- Final Data Model render retained as local QA evidence outside the repository.
- The 60,106-node large-model gate remained within bounds after the presentation changes.

