# ARStack IED Simulator -> external IEC 61850 client Manual Acceptance

This is the manual Windows acceptance gate for the simulator branch. Hosted CI proves the same MMS endpoint with ARStack wire probes; this procedure adds an independent external IEC 61850 client client check. It is interoperability evidence, not IEC 61850 conformance certification.

## Package layout

Keep these files in the same extracted directory:

- `arstack_ied_simulator.exe`
- `ariec61850_ied_simulator_server.exe`
- the Qt DLL/plugin/QML runtime deployed by `windeployqt`
- `minimal-station-brcb.scd`

The GUI intentionally launches `ariec61850_ied_simulator_server.exe` from beside the GUI executable.

## Baseline connection

1. Start `arstack_ied_simulator.exe`.
2. Open `minimal-station-brcb.scd`.
3. Select IED `SIM_IED`.
4. Select listener `127.0.0.1` for a same-PC test.
5. Use MMS port `102` unless another process already owns it.
6. Press **Start** and confirm the simulator reports that the MMS endpoint is listening.
7. In external IEC 61850 client, create/connect an IED at `127.0.0.1`, port `102`.

If Windows Firewall prompts for the simulator server, allow Private-network access for the local interoperability test.

## Acceptance gates

### A. Association and model browse

external IEC 61850 client must associate without TCP reset or reconnect loops. Browse must expose domain `MU01LD0` and the logical-node/object hierarchy from the SCL, including leaves that are not members of a DataSet. The structural-only regression leaf is `TCTR1$MX$AmpUnmapped$instMag$i`.

### B. Read exact leaf values

Confirm external IEC 61850 client can read at least:

- `MU01LD0/TCTR1$MX$Amp$instMag$i`
- `MU01LD0/XCBR1$ST$Pos$stVal`
- `MU01LD0/XCBR1$ST$Pos$q`
- `MU01LD0/XCBR1$ST$Pos$t`

Quality must be represented as IEC 61850 Quality / MMS BIT STRING(13), and Timestamp as MMS UTC time rather than text placeholders.

### C. Live GUI edit without reconnect

Keep the external IEC 61850 client association open. In the simulator GUI, change a writable leaf value, then read the same object again in external IEC 61850 client. The new value must be visible on the existing association. Repeat once for a scalar and once for either `q` or `t`.

### D. Reporting

Browse report-control objects under `LLN0` and verify the fixture exposes both URCB and BRCB controls. For the first independent-client gate, enable `LLN0$RP$URCB01`, request GI, and require an InformationReport on the same association. Then enable `LLN0$BR$BRCB01`, change a member of `LLN0$dsGO` in the simulator, and require a buffered report containing an EntryID.

### E. Controls

The fixture carries Direct/SBO normal/enhanced control models for `GGIO1.SPCSO1` through `GGIO1.SPCSO4`. After browse/read/reporting gates are green, exercise the models in external IEC 61850 client. A successful operation must update the associated `ST` value; enhanced models must complete through CommandTermination rather than treating the confirmed MMS Write response as final command completion.

## Result to capture

Capture one external IEC 61850 client screenshot showing the connected server tree, one screenshot showing a live value changed from the GUI and re-read without reconnect, and if reporting is tested, one report view showing GI/event delivery. Record the ARStack commit SHA, external IEC 61850 client version, Windows version, selected IP/port, and whether each gate A-E passed.

If a gate fails, preserve the exact object reference, external IEC 61850 client error text, simulator status/log output, and a short Wireshark capture filtered to `tcp.port == 102`. That evidence is sufficient to compare the external client behavior with the already-automated ARStack wire regression without guessing.
