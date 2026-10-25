# Install ARStack Studio

**[M3.H.3 Official Stable Release](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h3-20261025)** · **[Product page](https://masarray.github.io/arstack61850/)**

## Windows 10/11 (x64)

Download **[`ARStack-Studio-M3H3-win-x64-setup.exe`](https://github.com/masarray/arstack61850/releases/download/processbus-sim-m3h3-20261025/ARStack-Studio-M3H3-win-x64-setup.exe)** and follow Setup. The matching portable package is `ARStack-Studio-M3H3-win-x64-portable.zip`. The official installer is built from the accepted Windows CI preview binary and retains its historical internal installer identity; it does not silently replace older stable installs.

To download the newest installer through PowerShell, verify its SHA-256 and open Setup automatically:

```powershell
$script = Join-Path $env:TEMP 'arstack-install.ps1'
Invoke-WebRequest 'https://raw.githubusercontent.com/masarray/arstack61850/main/scripts/install-windows.ps1' -OutFile $script
& $script
```

To download a verified copy without starting the installer, run `& $script -DownloadOnly`.

## Linux (x86_64)

For Linux x86_64, download `ARStack-Studio-linux-x86_64.AppImage` from the [M3.H.3 stable release](https://github.com/masarray/arstack61850/releases/latest). Mark it executable (`chmod +x`) and open it. You can also install it into your home folder with:

```bash
curl -fLsS https://raw.githubusercontent.com/masarray/arstack61850/main/scripts/install-linux.sh -o /tmp/arstack-install-linux.sh
bash /tmp/arstack-install-linux.sh
```

The install script verifies the matching published SHA-256. The M3.H.3 official stable release contains the tested Linux AppImage.

## First lab experiment

1. Open **ARStack Studio** and start from the built-in 4I+4V profile.
2. To publish actual Ethernet Sampled Values, connect a compatible ESP32-P4 and install the **matched firmware included with that release**.
3. Select a simulated PTP source, set waveform values, and press **Start**.
4. Inspect SV and PTP traffic on a dedicated test switch or network segment.

**Try a relay/BCU Process Bus lab:** configure an SV stream's APPID, MAC, VLAN and svID to match your subscriber's expected inputs, press Start and capture real Layer-2 traffic with Wireshark. ARStack also publishes internal PTP SOURCE traffic for lab timing simulations. With the ESP32-P4 powered, closing Studio does not stop SV; reopening Studio adopts the actual board state. Use the firmware paired with this release. [M3.H.3 on-wire lab capture](releases/PROCESSBUS_AUTONOMOUS_M3H3_OFFICIAL_20261025.md).
