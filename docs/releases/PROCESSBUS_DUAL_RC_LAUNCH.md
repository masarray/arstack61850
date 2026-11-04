# Process Bus Simulator — Dual ESP32-P4 public RC1 launch intent

**Candidate tag:** `processbus-sim-dual-rc1-20261105` (PUBLIC PRERELEASE, not stable).
**Authority:** `AGENTS.md` §0: emulate a real IEC 61850 Merging Unit and GPS-like PTP Grandmaster on isolated lab Ethernet. Do not gate on physical GNSS/UTC traceability or downgrade the deliberate simulated-global `smpSynch=2` profile.

This intent triggers the existing-preview **artifact-promotion** job only after the PR is merged into `main` with a `release(rc):` squash-merge title. It **does not add another Qt or ESP-IDF build**. The publisher resolves the just-merged PR and its exact-head successful native preview workflow, downloads the three already-verified artifact families, rechecks their source/firmware catalog identity, and publishes a GitHub prerelease with Windows portable/installer, Linux AppImage, pre-v3 and v3.x firmware images/catalog and checksums. Fails closed on any missing or mismatched evidence; does not touch `processbus-sim-m3h3-20261025`.

**Product scope:** independent canonical 4I+4V SV streams from SCL and selectable GPS-style simulated PTP SOURCE; the M4 integrated software can control three MUs on one sample grid. Lab SV/PTP packets are genuine Ethernet frames, generated electrical and time measurements are simulation inputs.

**Candidate acceptance:** existing PR-head Windows/Linux/ESP32 checks green, 2 distinct revision images from same source SHA, correct flash catalog and installer contents, intact schema/manifest hashes, preserved global-GM emulation and negative regression. Engineering limitations of new v3.x boards and full 3-MU physical HIL remain specifically identified, rather than inventing certification requirements or releasing unverified hardware promises.

**Release channel:** RC/pre-release only pending wider field compatibility testing; once accepted it may be promoted without changing historical release assets. Do not present this as a production energized-substation transmitter.
