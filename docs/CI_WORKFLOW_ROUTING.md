# ARStack61850 — CI routing and runner-budget contract

**Purpose:** fast iterations without sacrificing reliable release validation.
A normal WIP/experimental feature push must NOT start a firmware matrix,
Windows package, sanitizer run, or broad unrelated subsystem regression.

## Routing by lifecycle

| Event | What runs | What must **not** run |
| --- | --- | --- |
| Draft PR, opened/synchronized | Lightweight routing policy and deliberately opted-in checkpoint jobs | Heavy ESP-IDF, Windows Qt/installer, GCC+Clang+MSVC, sanitizers, control/MMS matrices |
| Explicit single checkpoint | Engineer selects the exact workflow/ref via `workflow_dispatch`, or a temporary opt-in one-job gate | Unrelated workflows and full platform/release packaging |
| PR `ready_for_review`, later synchronize | All *relevant* path-filtered CI, including specialized required regression | Unrelated subsystem matrices |
| `main` integration push | Relevant path-scoped integration tests (including security, control, firmware) | All-workflows-on-every-commit |
| Tagged `v*` release or manual release dispatch | ARStack Studio Windows installer + release firmware as requested | Installer on ordinary PR commits |

Every workflow keeps its own **`paths`** filters for ready PR checks.
When a Draft PR is updated, GitHub may show a workflow **skipped** record;
that record is not a passed test and must not allocate the heavy job runner.
We cannot prevent the GitHub PR event itself without also losing up-to-date
Ready-PR checks on `synchronize`.

## Firmware and PTP–SMV strategy

- **ESP32-P4 PTP Lab Profile** and **ESP32-P4 SMV Injector**:
  feature-branch `push` events are disabled; automatic `push` is scoped to
  `main` plus relevant paths. `workflow_dispatch` is supported on exact
  feature refs to create a test firmware artifact intentionally.
- `workflow_call` in the existing SV firmware workflow remains available
  to the manually/tag-triggered Studio release process.
- Draft PTP/SV development can use a **single explicit target** in the
  existing workflow (e.g. M2/M3 phase regressions plus guarded SOURCE build),
  then remove the opt-in after receiving exact-SHA evidence.
- The default firmware must never be made falsely green by treating a
  **skipped** experimental test as proof of PTP synchronization or IED lock.

## Studio, IED Simulator and protocol tests

- Studio installer (`arstack-studio-release.yml`) triggers only from a
  version tag or explicit `workflow_dispatch`, not normal PRs.
- Studio Qt, Simulator Qt and Windows lifecycle jobs remain path-scoped for
  Ready PRs but skip expensive jobs on Draft.
- Interoperability server no longer matches *all* IEC61850 headers:
  a standalone PTP time-sync change does not invoke MMS-server CI.
- Core C++, embedded and security jobs use existing PR affected-file paths
  on `main` too. Control/BRCB specialized CI uses its own affected paths
  on `main`, instead of rebuilding every Control variant for unrelated UI.
- Superseded PR heads are cancelled via workflow-specific `concurrency`
  groups. Avoid workflows that cause additional commits or dispatch siblings
  merely to perform ordinary validation.

## Working discipline

1. Implement a coherent milestone in one local/remote batch. Avoid push after
   every line edit, generated formatting change, or README adjustment.
2. Check what can be tested without hardware first (portable CTest/lint).
3. Choose **one** targeted CI checkpoint on the selected feature SHA.
4. Resolve failures, then mark Ready when normal path-scoped checks should run.
5. Before integration/release, insist on relevant **non-skipped** exact-head
   results; select the release workflow explicitly if an installer is needed.
6. A CI routing policy modification is always checked by the cheap
   `ci-routing-policy.yml` job, which runs
   `scripts/check_ci_trigger_policy.py`.

**Cost/accountability:** A skipped Draft run is acceptable metadata.
A repeated 25-minute ESP-IDF image pull, full Windows package, irrelevant
protocol matrix or repeated action per WIP commit is not.

### M3-specific bounded Draft checkpoints

The two allowlisted opt-in job exceptions are `wire-audit-checkpoint` in C++ CI (`[run-wire-audit]`) and `phase-guard-checkpoint` in ESP32-P4 PTP Lab (`[run-phase-guard]`). They must stay single-job, non-matrix Draft gates; the full GCC/Clang/MSVC and SOURCE/RECEIVER builds remain disabled on Draft. Remove PR-body markers immediately after exact-head checkpoint validation. This exception is intentionally narrow and cannot be extended silently to unrelated workflows.
