# ARStack Studio P0 Release Freeze

## Freeze baseline

The P0 release candidate is frozen from commit:

`ed46f3bed73d398d71be2eacf34c4995325c3cad`

This exact source head completed the ARStack Studio Qt, ARStack Studio Release, RC2 acceptance-policy, ESP32-P4 injector, embedded profile, C++, security/evidence, and related branch gates successfully.

The freeze branch is:

`release/arstack-studio-p0-freeze`

PR #79 remains open and unmerged. Issue #83 remains the acceptance authority.

## Freeze state

Current state: **FROZEN_PENDING_PHYSICAL_ACCEPTANCE**.

A green CI/release pipeline is necessary but not sufficient for public release. The remaining release authority is physical acceptance on the real ESP32-P4 hardware, including the RC1 USB/session matrix and RC2 functional/4000-fps evidence.

Do not merge PR #79, create the final release tag, or publish the public release until issue #83 records physical acceptance.

## Scope rule

After the freeze baseline, no new feature, UX expansion, protocol expansion, refactor, layout widening, performance experiment, or unrelated cleanup is allowed.

The only permitted production changes are **release blockers**. Every blocker change must be declared in `apps/arstack_studio/release/freeze-manifest.json` before it can pass the release-freeze guard.

Each exception entry must identify:

- exact repository path;
- blocker issue/reference;
- failure/risk being addressed;
- rationale for changing frozen production code;
- verification required to prove the blocker is fixed without weakening existing gates.

Governance-only changes to this document, the freeze manifest, and the release-freeze workflow are permitted without an exception.

## Frozen product boundary

The public P0 boundary remains:

- 4 current + 4 voltage channels;
- 4000 samples/s reference profile;
- one ASDU;
- 64-byte sample payload;
- 16 ordered FCDA leaves;
- no automatic Start after reconnect/update;
- explicit, fail-closed firmware write;
- session lease retained as the hard-crash RUNNING failsafe;
- `smpSynch=0` unless separate measured disciplined-clock evidence exists.

No broader runtime layout or synchronization claim may enter during the freeze.

## RC evidence pinned at freeze

- ARStack Studio Release run: `34863664544` — PASS.
- ARStack Studio Qt run: `34863664380` — PASS.
- RC2 Acceptance Policy run: `34863664307` — PASS on Windows and Linux.
- Windows candidate artifact: `arstack-studio-windows-x64`, artifact `10358737281`, digest `sha256:6a74b60b11706e3eab723bfb28827f2138430ce7617d47fa09bcba6617fd90c5`.
- Firmware artifact: `arstack-esp32p4-smv-firmware`, artifact `10358024903`, digest `sha256:1cc93abdb60c1e2575ade9ca7eb1a85f7d4ba7cdb52d72280677085fef1676f4`.
- RC2 physical QA kit: artifact `10355888453`, digest `sha256:2dc83c7ce784dc861eeec6be5b59723ce7c8b9fd23ecc0adbc54fb109518bb3d`.

## Remaining release gates

Release remains blocked until the physical evidence in issue #83 proves all of the following:

1. repeated cold/open cycles produce zero false Install Firmware prompts and converge to bounded READY/actionable error;
2. READY and RUNNING unplug/replug recovery obeys semantic device identity and never auto-Starts;
3. serial/espflash handoff has no COM ownership contention;
4. bundled 4I+4V profile deploys, starts, stops, zeroes, and accepts live edits correctly;
5. retained run is at least 3600 seconds at the 4000-fps target with `missed=0` and canonical `TX fail=0`;
6. independent capture proves the frozen SV identity/layout/counter contract and `smpSynch=0` truth boundary;
7. `arstack_rc2_acceptance` returns exit code 0 for evidence tied to the exact candidate SHA.

## Blocker-fix protocol

If a physical RC failure requires code changes:

1. create or reference a blocker issue;
2. add the exact changed path(s) and evidence requirements to the freeze manifest;
3. make the minimum fix only;
4. rerun the full exact-head Qt, Release, RC2 policy, embedded/injector, security and relevant core gates;
5. repeat the affected physical acceptance case;
6. move the freeze baseline only after the new exact head is fully green and the blocker is documented.

A freeze-baseline move is a deliberate release-management action, not an incidental commit.
