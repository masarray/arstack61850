# ARStack Studio native Windows preview — side-by-side operator candidate

**Scope:** Build the existing native Qt/C++/QML `ARStackStudio.exe` from the exact
GitHub pull-request head. Package a Windows installer/portable ZIP that a user
can open directly. There is **no web UI, external Python tool, or manual flash
file selection** in the operator workflow.

## Firmware selection and provenance

- The candidate is deliberately **GUI-only**. It uses the public v0.1.1
  firmware, manifest and standalone flasher from the [published v0.1.1
  portable release](https://github.com/masarray/arstack61850/releases/tag/v0.1.1).
- CI verifies the exact 46 MB source ZIP's SHA-256:
  `27dfa5374afe9ac9f9235d86dfa26131a3d2f3d26a73205f1fa188a6a3d9ee4c`.
  It copies **only** firmware, tools and their licenses from this authenticated
  baseline into the newly compiled GUI package. It does not silently bundle
  firmware from the experimental M3 branch.
- A `PREVIEW-PROVENANCE.json` file ties the compiled GUI commit to that
  immutable public firmware archive; this is a traceability record, not a
  firmware cryptographic signing scheme.
- Firmware compatibility and packaged operation are checked using **Studio's
  existing native firmware manifest validator**, not a parallel Python parser.

## Isolation policy

- The NSIS packaging script supports an opt-in `ARSTACK_PREVIEW` channel. It
  has a distinct installation directory, uninstall registry key, program-menu
  folder, display name, and uninstaller; **normal stable packaging defaults are
  unchanged**.
- The preview GUI advertises `0.1.1-preview` in its window title. This is the
  *GUI channel label*, not a new embedded firmware version.
- Studio keeps its existing cross-process single-instance serial-port lock;
  stable Studio and preview **must not control the same device concurrently**.
- Only a PR-triggered workflow builds preview candidates. It never creates a
  GitHub Release, tag, merges to main, or overwrites v0.1.1.
- Artifact lifetime is seven days. Installer and portable ZIP SHA-256 files are
  shipped together.

## Gates

1. Build the exact PR-head Qt/C++ Studio Windows executable with warnings-as-errors.
2. Run the production deterministic device/session harness.
3. Verify the pinned v0.1.1 release source archive checksum before extracting
   firmware/flasher from it.
4. Run Studio's own profile/firmware/P0 policy and application-lifecycle checks
   **against the staged Windows executable and firmware**; invoke bundled
   espflash standalone to verify its executable availability.
5. Produce an independent *preview-only* NSIS installer and portable ZIP.
6. Silent-install the preview into a clean Windows test location; rerun native
   firmware/P0/lifecycle checks, verify preview-specific registry identity and
   absence of stable registration, and silently uninstall.
7. Upload the Windows artifacts only if every gate succeeds.

Hardware PTP phase accuracy, external GM/PPS and IED acceptance are not proven
by a Windows installer. The SOURCE/RECEIVER M3 embedded firmware is not included;
corresponding features will appear unavailable on devices without the required
firmware capability. `smpSynch AUTO` remains fail-closed.

**Operator:** Download the Windows preview setup from the successful GitHub
Actions run's Artifacts area and launch `ARStack-Studio-Preview-win-x64-setup.exe`.
The preview installs separately from the public Studio and operates through
the GUI. It is an engineering evaluation candidate, **not a stable release**.
