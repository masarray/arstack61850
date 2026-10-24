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

For Linux x86_64, download `ARStack-Studio-linux-x86_64.AppImage` from the [latest stable release](https://github.com/masarray/arstack61850/releases/latest) when listed. Mark it executable (`chmod +x`) and open it. You can also install it into your home folder with:

```bash
curl -fLsS https://raw.githubusercontent.com/masarray/arstack61850/main/scripts/install-linux.sh -o /tmp/arstack-install-linux.sh
bash /tmp/arstack-install-linux.sh
```

The install script checks the matching published SHA-256. It will explain when the current release does not yet contain a Linux build rather than downloading an older or unverified binary. The AppImage build is only published after Linux CI succeeds.

## First lab experiment

1. Open **ARStack Studio** and start from the built-in 4I+4V profile.
2. To publish actual Ethernet Sampled Values, connect a compatible ESP32-P4 and install the **matched firmware included with that release**.
3. Select a simulated PTP source, set waveform values, and press **Start**.
4. Inspect SV and PTP traffic on a dedicated test switch or network segment.

No external timing reference is required for the basic simulation. Use the [release notes](https://github.com/masarray/arstack61850/releases/latest) for the exact validated capabilities and device/firmware pairing.
