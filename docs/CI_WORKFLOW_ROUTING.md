# CI routing contract — ARStack61850

The CI contract is **risk-scoped PR checks, full integrated `main` checks, and manual/on-demand release packaging**. A new feature commit must never trigger unrelated domain matrices.

## Pull request matrix

| Changed area | Normal PR gate | Additional relevant gates |
| --- | --- | --- |
| `apps/ied_simulator/qml/**` / Workbench UI | IED Simulator Qt | SCL-assisted Workbench only for its specifically listed C++ model files |
| `apps/ied_simulator/packaging/**`, app CMake or entrypoint | IED Simulator Qt | IED Simulator Release Hardening |
| `include/**`, `src/**`, root `tests/**`, `tools/**` | C++ CI, Security and Evidence | Specialized profile gates matching the changed protocol |
| BRCB/RCB sources | C++ CI, Security and Evidence | BRCB Hard / Dynamic RCB where relevant |
| control interoperability sources | C++ CI, Security and Evidence | Control Interop Harness |
| embedded sources or embedded-facing protocol core | Embedded Profile CI | Other firmware / protocol-specific workflows through their own paths |

- **PR**: path-scoped. Superseded PR heads are cancelled using workflow-specific concurrency groups.
- **Feature branch push**: no duplicate workflow run when an equivalent PR run already exists. The patched workflows run on push to `main` only (historic explicit special branches remain where applicable).
- **`main`**: preserve full integration CI where configured. It is not path-trimmed to optimize feature iteration.
- **Windows Workbench release package**: run automatically on release-impacting PRs and integrated `main` changes, or manually with `workflow_dispatch` for an explicit bench/release candidate. Normal QML-only PR iteration should not package an installer on every commit.
- **Before merging a release candidate**: explicitly run the Windows packaging/release acceptance workflow on the exact release candidate head, in addition to scoped PR CI. No hardware proof is implied by CI.
- **Workflow files themselves**: are included in their own path filters so trigger changes receive acceptance runs. The lightweight CI Routing Policy gate checks trigger invariants.

Do not broaden unrelated PR `paths` to `apps/ied_simulator/**` or remove concurrency without updating `scripts/check_ci_trigger_policy.py`. Batch related changes into one commit before asking CI to validate; prefer failed-job reruns rather than repushing formatting edits.
