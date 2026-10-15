# ARIEC61850 engine and ARSAS Explorer to ARStack native capability map

Integration baseline: 2026-10-15. Target: installed C++/Qt/QML ARStack IEC 61850 Workbench in apps/ied_simulator. The apps/arstack_studio product and ESP32-P4 installer are separate. Do not assume changes here ship in that installer.

## Verified references

- ARIEC61850 masarray/ARIEC61850 main e5deed1d8aa11d97991695c6e390baafea7ab797: README.md, apps/AR.Iec61850.IedDiscovery, apps/AR.Iec61850.EngineeringWorkbench, src/AR.Iec61850.
- ARSAS masarray/arsas main 45348eccf24e941dd185e5fb729217923846c610: MainWindow.xaml, MainWindow.ExplorerSignalGrid.cs, MainWindow.ExplorerHeroActions.cs, MainWindow.SclLiveModelAuthority.cs and MainWindow.SharedStaticDataSetAuthority.cs.
- ARStack baseline c8aabf292c370993950b8682718cd56b77f2cabf.

## Integration boundaries

| Reference capability | Existing authoritative C++/Qt owner | Required work |
| --- | --- | --- |
| Live discovery and typed SCL | IedEngineeringContextController, C++ MMS/SCL model | Port only missing validated semantics; never introduce a second .NET engine or XML semantic tree |
| Confirmed MMS Read/Write | MmsClientController, IedBrowserFleetController | Retain exact-source associations, generation checks and explicit Read |
| LD/LN/DO/DA, CDC, FC, q/t | MmsLiveTreeModel, MmsEngineeringEvidence.hpp | One indexed canonical model; M2K/M2L accepted; no guessed unit/multiplier |
| DataSet ordered FCD/FCDA | MmsReportController, MmsLiveTreeModel | Preserve member order, FC, indexed aliasing and 64-child lazy expansion |
| URCB/BRCB and received report | MmsReportController, BrowserReportMemberProjection.hpp | Preserve actual frame identity, member ordinal, replay and report-first static path |
| Guarded control, SG and files | Existing C++ controllers | Transfer demonstrated missing capability only with negative/lifecycle tests |
| ARSAS live signal grid | FleetGlobalDataPane.qml, BrowserGlobalDataPane.qml | Already present after M2K; future work is genuine acquisition evidence and rendered visual QA |
| ARSAS first-run actions | IedBrowserWorkspace.qml | Reuse existing Open SCL, Connect and Discover; improve empty state based on native screenshot QA |
| Selected DA/FCDA inspector | BrowserDataModelPane.qml, BrowserDataSetPane.qml | Show every selected DA's value, read state, FC, q/t and source without implicit Read |

## Engineering acceptance

1. Open real SCL/CID, resolve IED, navigate LD/LN/DO/DA offline.
2. Connect using the selected engineering context; show actual sample, NOT READ, failed or unresolved evidence correctly.
3. Select a DataSet FCD/FCDA and inspect an ordinary DA, not only q/t leaves; no network action on selection.
4. Explicit bounded Read updates only the proper IED association, not a parallel UI cache.
5. Select RCB, inspect effective state, enable under guards and map an actual InformationReport in original member order.
6. Disconnect/change source without stale context or callbacks.
7. Validate native rendered selected-state screenshots, 60k+ model, 8-IED isolation, malformed inputs, reconnect, and actual Windows Workbench installer separately.

This is an implementation map, not a claim of physical interop or completed feature/visual parity.
