# CI routing contract — ARStack61850

The CI contract is **draft PRs do not consume heavy runner minutes, targeted ready-for-review gates, risk-scoped `main` integration, and manual/tag-only release packaging**. A new feature commit must never trigger unrelated domain matrices.

## Pull request matrix

| Changed area | Normal PR gate | Additional relevant gates |
| --- | --- | --- |
| `apps/ied_simulator/qml/**` / Workbench UI | IED Simulator Qt | SCL-assisted Workbench only for its specifically listed C++ model files |
| `apps/ied_simulator/packaging/**`, app CMake or entrypoint | IED Simulator Qt | IED Simulator Release Hardening |
| `include/**`, `src/**`, root `tests/**`, `tools/**` | C++ CI, Security and Evidence | Specialized profile gates matching the changed protocol |
| BRCB/RCB sources | C++ CI, Security and Evidence | BRCB Hard / Dynamic RCB where relevant |
| control interoperability sources | C++ CI, Security and Evidence | Control Interop Harness |
| embedded sources or embedded-facing protocol core | Embedded Profile CI | Other firmware / protocol-specific workflows through their own paths |

- **Draft PR**: expensive C++ / sanitizers / MSVC / Qt / ESP-IDF / PTP / interop jobs are **skipped**. A Draft can be updated without restarting expensive runs. **Ready PR**: path-scoped jobs run on ready_for_review and synchronize. Superseded PR heads are cancelled.
- **Feature branch push**: no repeated ESP-IDF/Windows builds. Only selected explicit historic special branches retain their own scoped push gates. Favor a Draft PR until an acceptance checkpoint, then mark Ready or manually dispatch the relevant test.
- **`main`**: production integration is path-scoped (including C++ and security) and superseded heavy runs may be cancelled as the branch advances. Keep the newest HEAD green before tagging.
- **Studio / PTP / legacy SMV GUI**: feature branch push does not duplicate the PR validation; push is scoped to `main`.
- **ESP32-P4 firmware / PTP lab**: feature pushes do **not** run ESP-IDF builds. Run on scoped ready PRs, `main` changes, or intentionally from `workflow_dispatch` on the exact branch/commit for hardware testing.
- **ARStack Studio release package**: only `workflow_dispatch` for explicit test installers or `v*` tags for official release. PR changes do **not** trigger full installer packaging. ARStack Studio Qt smoke runs on relevant ready PRs.
- **Before merging a release candidate**: explicitly run the Windows packaging/release acceptance workflow on the exact release candidate head, in addition to scoped PR CI. No hardware proof is implied by CI.
- **Workflow files themselves**: are included in their own path filters so trigger changes receive acceptance runs. The lightweight CI Routing Policy gate checks trigger invariants.

Do not broaden unrelated PR `paths` to `apps/ied_simulator/**` or remove concurrency without updating `scripts/check_ci_trigger_policy.py`. Batch related changes into one commit before asking CI to validate; prefer failed-job reruns rather than repushing formatting edits.

## Engineering checkpoint protocol

1. Work in Draft PR without repetitive workflow runs for each edited file. Validate formatting and small tests locally or at a deliberate checkpoint.
2. Run a **single targeted workflow_dispatch on the exact feature ref** for an early firmware artifact; explicitly name the commit SHA in test evidence.
3. Mark PR Ready only when code is coherent. Relevant path-scoped GCC/Clang/MSVC, firmware, security and Qt checks then run; avoid pointless pushes to wake runners.
4. Build the full Windows installer **only on demand or tag**, never for every protocol diff.
5. Merge only after relevant required checks at the current head and investigate missing/stale checks. Do not confuse a skipped Draft check with passed test evidence.

A GitHub workflow record for a Draft may still show as *skipped*; it must consume **zero expensive job runners**, not requeue 15-minute builds. Manual dispatch remains available and is not skipped.
