# M4-P0E3C — Verified three-MU group START/STOP (engineering preview)

This implementation reuses the sole ESP32-P4 publisher, physical-link owner,
shared PTP/sample epoch, three canonical ARM3 profiles, and Studio serial worker.
It does not activate on reconnect, GUI discovery, SCL import, or firmware flashing.

## Operator flow

Open exact 3-IED SCD → Deploy 3 MU (ARM3 COMMIT/SHOW/SELECT) → Apply 3 MU
(three independent CFG COMMIT/READBACK) → press the existing START button.
The current three drafts must hash-identically match the applied bank proofs.
SCD must be MATCHED to the board's selected 3-STREAM roster, the Ethernet PHY
must report UP, and firmware must advertise MULTISV-GROUPSTART-V1.

## Device protocol

`M4START <unique-decimal-token> <boot16hex> <roster16hex> <MU1-gen-dec> <MU1-hash16hex> <MU2-gen-dec> <MU2-hash16hex> <MU3-gen-dec> <MU3-hash16hex>`

The existing UART control task verifies: current boot, immutable committed
roster hash, selected mode, no active upload, TX mask 0x07, all three current
bank generations/hashes, unique request token, fresh operator heartbeat,
PHY UP and STOPPED publisher. A legacy `START` cannot start a selected M4
roster. No extra timer/task, heap allocation, or work is added to 250us TX.

On acceptance, the existing publisher task starts the existing 3-SV path.
Studio waits for exact `M4START accepted token/boot/roster` then sends `M4SHOW`.
Firmware returns one board-control-plane `M4STATE token/boot/roster/generation/
selected/mask/running`. Only a matching fresh response with **all 3 TX-enable bits** marks RUNNING.
Group STOP requires matching boot/roster and `running=0`, but permits any
valid 3-bit per-MU mask because independently stopped streams must not block
verification of the shared publisher STOP. Neither control-plane state proves
any packet on physical Ethernet.

The existing priority `STOP` cancels a pending group START; Studio retires
unsent exclusive commands and requires `STOP accepted` plus a fresh
`M4STATE running=0` before calling STOPPED verified. A timeout or transport
error is **UNCERTAIN**, never fabricated as STOPPED. In that case the existing
STOP action remains available even if RUNNING telemetry was not received. USB disconnect clears
local proofs without sending STOP or automatically re-arming the board.

## Acceptance boundary

Software gates: Qt/Linux/Windows harnesses, ESP32-P4 exact firmware image,
negative roster/hash/boot/mask/draft and lifecycle tests, paired preview.
Hardware remains **PENDING** until matched-board independent PCAP proves
three 4I+4V SV streams at 4000 frames/s each (12000 aggregate), identical
sample epoch, correct SCL MAC/APPID/VLAN and real values, plus sustained
TX budget and explicit STOP/reconnect behavior. Preserve M3.H.3 stable assets.
