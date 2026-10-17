# F4 — Real IED / external IEC 61850 client External Acceptance

F4 is the external interoperability gate for PR #82. Internal simulator/loopback CI is necessary but is **not** external acceptance.

## Acceptance lanes

### A. external IEC 61850 client -> ARStack simulator server (mandatory for issue #95)

Use the same CID/SCL model used for the golden external IEC 61850 client capture. Run the ARStack Workbench/server in normal/process mode, then connect external vendor external IEC 61850 client to the ARStack endpoint.

Required evidence:

1. Association completes without an early disconnect.
2. Discover IED Model from IP completes far enough to show a non-empty Reports inventory.
3. DataSets are visible/readable.
4. URCB and BRCB roots are visible, typed and readable.
5. At least one RCB can be enabled/reserved as appropriate and GI produces an InformationReport.
6. Association remains alive through FileDirectory. A FileDirectory request must not be treated as success merely because earlier discovery worked.
7. The complete TCP/102 session is retained in PCAP/PCAPNG together with Workbench/server diagnostics.

FileDelete is **not** part of the real-IED safety gate. If FileDelete interoperability is exercised, do it only against the ARStack sandbox file root with a disposable test file and explicit `--allow-file-delete`.

### B. ARStack Workbench -> real IED (field/client lane)

This lane validates the client against a real device or trusted vendor simulator.

Required evidence:

1. TCP/102 and MMS association succeed.
2. Live discovery completes and typed reads work.
3. DataSet/URCB/BRCB inventory is populated when exposed by the device.
4. A safe report-control cycle can be completed: enable/reserve as required -> GI -> InformationReport -> disable/release.
5. FileDirectory is read-only by default. Optional download must complete with FileClose confirmed.
6. Do **not** perform remote FileDelete on a field IED as part of F4. Do not perform control writes unless the point is a dedicated bench/test point and the operator has explicitly authorized it.
7. Capture the complete TCP/102 session and Workbench diagnostics.

## Windows field kit

The **F4 Real IED Acceptance RC** workflow can be dispatched for any chosen source revision. Changes to this F4 workflow, its field kit, or the Windows packaging contract also run exact-PR-head CI and post-merge main packaging. Do not use a field kit whose embedded `BUILD_HEAD.txt` differs from the exact source revision being accepted.

The F4 workflow produces a Windows artifact containing:

- current Workbench installer and portable ZIP;
- current `ariec61850_ied_simulator_server.exe`;
- live discovery/read/RCB/file-transfer probes;
- `collect-f4-evidence.ps1`;
- `ACCEPTANCE_RESULT_TEMPLATE.md`;
- `BUILD_HEAD.txt` and SHA-256 checksums.

### Capture preparation

Install Wireshark/Npcap with both `dumpcap.exe` and `tshark.exe`. The capture is intentionally **bound to the exact target IPv4 address and MMS port**; unrelated traffic on TCP/102 does not qualify as acceptance. The collector refuses a missing/invalid kit SHA, missing tshark, invalid NIC index, zero observed target packets, or an installed/extracted Workbench whose SHA differs from the kit.

List capture interfaces:

```powershell
& "C:\Program Files\Wireshark\dumpcap.exe" -D
```

Note the numeric interface id. Start the ARStack server/Workbench or make the real IED reachable **before** starting evidence collection.

Example, external IEC 61850 client testing ARStack on loopback:

```powershell
.\collect-f4-evidence.ps1 `
  -Mode ExternalClientToARStack `
  -TargetIp 127.0.0.1 `
  -InterfaceId 1 `
  -DurationSeconds 180
```

Example, ARStack testing a real IED:

```powershell
.\collect-f4-evidence.ps1 `
  -Mode ARStackToIED `
  -TargetIp 192.168.1.10 `
  -InterfaceId 4 `
  -WorkbenchDirectory 'C:\Program Files\ARStack IEC 61850 Workbench' `
  -DurationSeconds 180
```

For the real-IED lane, `-WorkbenchDirectory` is **mandatory** and may reference either the installed app or the extracted portable ZIP root (both contain `BUILD_HEAD.txt` from the same release). The directory must contain `arstack_ied_simulator.exe`. The external-client-to-ARStack lane may optionally supply it, but the collector cannot independently prove which server process is active: attach server diagnostics to close that gap.

During capture, perform the authorized steps above. The collector writes a timestamped folder containing `evidence.json`, a target-specific PCAPNG, exact source SHA, TCP preflight, SHA-256 and tshark-verified target packet/reset counts, and a **manual** result template. Capturing one TCP packet is not proof of MMS association, report reception, or hardware interoperability; inspect service-level frames and screenshots separately. Missing network visibility is **INCOMPLETE**, never PASS.

## Pass / close rule

Internal CI may mark the F4 **RC ready**, but issue #95 stays open until lane A is executed with real external IEC 61850 client and the captured evidence shows the required discovery/reporting behavior without an association-loss blocker. A simulator-only result must never be relabelled as external external IEC 61850 client acceptance.
