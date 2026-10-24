# Install ARStack Studio

**[Latest stable release](https://github.com/masarray/arstack61850/releases/latest)** · **[Product page](https://masarray.github.io/arstack61850/)**

## Windows 10/11 (x64)

Download the `win-x64-setup.exe` asset from **Latest stable release**, open it and follow Setup. The portable ZIP works without installing.

To download the newest installer through PowerShell, verify its SHA-256 and open Setup automatically:

```powershell
$script = Join-Path $env:TEMP 'arstack-install.ps1'
Invoke-WebRequest 'https://raw.githubusercontent.com/masarray/arstack61850/main/scripts/install-windows.ps1' -OutFile $script
& $script
```

To download a verified copy without starting the installer, run `& $script -DownloadOnly`.

## Linux (x86_64)

When the latest GitHub release offers `ARStack-Studio-linux-x86_64.AppImage`, download it, mark it executable (`chmod +x`) and open it. You can also install it into your home folder with:

```bash
curl -fLsS https://raw.githubusercontent.com/masarray/arstack61850/main/scripts/install-linux.sh -o /tmp/arstack-install-linux.sh
bash /tmp/arstack-install-linux.sh
```

The install script checks the matching published SHA-256. It will explain when the current release does not yet contain a Linux build rather than downloading an older or unverified binary. The AppImage build is only published after Linux CI succeeds.

## Windows Package Manager / WinGet

WinGet uses an indexed, versioned package manifest. The release tooling prepares a manifest from the actual installer and SHA-256 once a release is final. The Microsoft community repository must accept the package **before** a public `winget install MasArray.ARStackStudio` command works; until then, use the Windows command above. A script or README alone does not register a WinGet package.

Maintainer command (requires a tagged release with a unique Windows package version):

```bash
python3 scripts/generate_winget_manifest.py --version X.Y.Z --installer-type inno
```

Review the generated YAML, validate with WinGet tooling, and submit it to [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs). Use `--installer-type nullsoft` for the earlier NSIS installer format.

## First lab experiment

1. Open **ARStack Studio** and start from the built-in 4I+4V profile.
2. To publish actual Ethernet Sampled Values, connect a compatible ESP32-P4 and install the **matched firmware included with that release**.
3. Select a simulated PTP source, set waveform values, and press **Start**.
4. Inspect SV and PTP traffic on a dedicated test switch or network segment.

No external timing reference is required for the basic simulation. Use the [release notes](https://github.com/masarray/arstack61850/releases/latest) for the exact validated capabilities and device/firmware pairing.
