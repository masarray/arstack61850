# ARStack Studio Inno Setup — GUI-first Windows installer

The native Qt/C++ Studio .exe is the only operator interface. The new installer
uses Inno Setup 6.7.1 (pinned in CI), keeping the existing NSIS script only
for historical v0.1.1 reproducibility.

## User-focused behavior

- **Install/update:** same Inno AppId detects an earlier installation in its
  channel and uses the previous install directory; no second "copy" is made.
  The next preview installer upgrades the previous preview through the same
  normal double-click GUI, not a Python script.
- **Running Studio:** Windows Restart Manager requests graceful application
  closure before overwriting running EXE/DLLs. Do not force-kill or silently
  resume SV output. For unattended CI, Inno uses /CLOSEAPPLICATIONS.
- **Safety:** preview and stable use **different** AppIds, uninstall registry
  entries, install folders and Start Menu shortcuts. Both still share
  Studio's cross-process serial-device single-instance lock.
- **No admin required:** per-user installation with PrivilegesRequired=lowest.
- **Remove:** Inno unregisters its own install, executable, tools, firmware,
  and shortcuts. Never delete unknown files or application/user data by
  wildcard. Keep operator-authored files untouched.
- **Traceability:** GUI commit, matching PR-HEAD ESP-IDF firmware source/manifest/hash,
  pinned public v0.1.1 **flasher tool** archive digest, and unqualified
  embedded PTP status are included with the preview.

## Acceptance

The historical published v0.1.1 firmware manifest does not declare
PROFILE-BINARY-V1, which the modern native Studio requires. The new bundle
must be built from the same source commit rather than accepting an obsolete
image or disabling the capability check.

The PR-triggered Windows workflow builds the exact PR-head Qt executable,
runs the native session harness, builds a fresh matching ESP32-P4 firmware
bundle through the shared ESP-IDF workflow from the exact PR HEAD,
verifies its manifest and image hash and PROFILE-BINARY-V1 capability, and
independently verifies the pinned public v0.1.1 archive's SHA-256 **only to
reuse the existing bundled flasher and licenses**. The historical firmware
is not compatible with the new Studio and is never staged. It stages the
matched firmware/tools into the preview GUI, executes packaged
application smoke, and produces a **single Windows Inno Setup .exe** plus a
portable ZIP. Then on a clean Windows runner it performs:

1. Silent first-time install to an isolated preview path.
2. Inno AppId registration and Studio firmware/policy/lifecycle checks.
3. Launch the GUI, request an in-place update via the same installer and
   confirm the first process was gracefully retired.
4. Confirm user-owned data survives upgrading.
5. Run the updated installed native firmware contract.
6. Uninstall, check owned EXE and AppId registration are gone while personal
   data is preserved, and ensure public NSIS stable registration was untouched.
7. Upload files only after all tests pass.

## Release safeguards and remaining boundaries

**The v0.1.1 NSIS installer cannot be silently taken over by a new Inno AppId
without a separate safe migration contract.** Preview deliberately never
uninstalls or modifies the public stable channel. A subsequent stable-channel
migration must validate ownership/path, safely stop Studio, **avoid running
the legacy NSIS uninstaller that recursively deletes user files**, and retire
only verified NSIS ownership records after the new Inno installation is in
place. A Windows CI fixture installs the actual hash-pinned v0.1.1 NSIS package,
tests migration, firmware validity, and data preservation, then uninstalls
Inno. This is an acceptance fixture only, never a published stable installer. Until then do not promote this to
public stable.

The operator needs only ARStack Studio and the bundled firmware manager;
Windows signing/trust and physical PTP/GM/PPS/IED interoperability remain
independent release gates.