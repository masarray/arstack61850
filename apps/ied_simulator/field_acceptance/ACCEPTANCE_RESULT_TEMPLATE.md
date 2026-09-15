# F4 External Acceptance Result

Build head: `______________________________`

Date/time: `______________________________`

Operator: `______________________________`

Scenario: `[ ] IEDScout -> ARStack   [ ] ARStack -> real IED`

Target / endpoint: `______________________________`

SCL/CID identity: `______________________________`

PCAP/PCAPNG SHA-256: `______________________________`

## A. IEDScout -> ARStack simulator server

- [ ] ARStack running in normal/process mode.
- [ ] MMS association completes.
- [ ] Discover IED Model from IP completes without early association loss.
- [ ] Reports inventory is non-empty.
- [ ] DataSets are visible/readable.
- [ ] URCB root is visible, typed and readable.
- [ ] BRCB root is visible, typed and readable.
- [ ] Safe RCB enable/reserve succeeds where required.
- [ ] GI produces an InformationReport.
- [ ] RCB disable/release succeeds.
- [ ] Association remains alive through FileDirectory.
- [ ] Full TCP/102 PCAP/PCAPNG retained.
- [ ] Workbench/server diagnostics retained.

Observed RCB/DataSet references:

```text

```

FileDirectory result / last successful service before disconnect, if any:

```text

```

## B. ARStack Workbench -> real IED

- [ ] TCP/102 reachable.
- [ ] MMS association accepted.
- [ ] Live discovery completes.
- [ ] Typed Read succeeds.
- [ ] DataSet/URCB/BRCB inventory is populated when exposed by device.
- [ ] Safe report cycle completes: enable/reserve -> GI -> InformationReport -> disable/release.
- [ ] FileDirectory is read-only and succeeds.
- [ ] Optional download completes with FileClose confirmed.
- [ ] No remote FileDelete was performed on the field IED.
- [ ] No control write was performed unless explicitly authorized on a dedicated bench/test point.
- [ ] Full TCP/102 PCAP/PCAPNG retained.
- [ ] Workbench diagnostics retained.

## Evidence review

TCP reset count: `__________`

Association disconnect point, if any: `______________________________`

First failing MMS service / invoke id, if any: `______________________________`

Relevant Wireshark frame numbers:

```text

```

Diagnostics excerpt:

```text

```

## Verdict

- [ ] PASS — external acceptance criteria satisfied.
- [ ] FAIL — blocker reproduced; keep issue open.
- [ ] INCOMPLETE — capture/evidence insufficient; rerun required.

Notes:

```text

```
