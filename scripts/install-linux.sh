#!/usr/bin/env bash
# Download and install the verified Linux AppImage from the latest stable release.
set -euo pipefail
repo="masarray/arstack61850"
api="https://api.github.com/repos/$repo/releases/latest"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
command -v curl >/dev/null || { echo 'Please install curl first.' >&2; exit 1; }
command -v python3 >/dev/null || { echo 'Please install Python 3 first.' >&2; exit 1; }
curl --fail --location --silent --show-error -H 'Accept: application/vnd.github+json' "$api" -o "$tmp/release.json"
readarray -t urls < <(python3 - "$tmp/release.json" <<'PY'
import json, sys
r=json.load(open(sys.argv[1]))
if r.get('draft') or r.get('prerelease'):
    raise SystemExit('Latest release is not public stable.')
a={i['name']:i for i in r.get('assets',[])}
name='ARStack-Studio-linux-x86_64.AppImage'
checksum='ARStack-Studio-linux-x86_64.sha256'
if name not in a or checksum not in a:
    raise SystemExit('Linux AppImage is not published for the latest release yet. Check GitHub Releases.')
print(a[name]['browser_download_url'])
print(a[checksum]['browser_download_url'])
PY
)
if (( ${#urls[@]} != 2 )); then exit 1; fi
curl -fLsS "${urls[0]}" -o "$tmp/ARStack-Studio-linux-x86_64.AppImage"
curl -fLsS "${urls[1]}" -o "$tmp/ARStack-Studio-linux-x86_64.sha256"
(cd "$tmp" && sha256sum -c ARStack-Studio-linux-x86_64.sha256)
opt="$HOME/.local/opt/arstack-studio"
bin="$HOME/.local/bin"
mkdir -p "$opt" "$bin" "$HOME/.local/share/applications"
install -m755 "$tmp/ARStack-Studio-linux-x86_64.AppImage" "$opt/ARStackStudio.AppImage"
ln -sfn "$opt/ARStackStudio.AppImage" "$bin/arstack-studio"
cat > "$HOME/.local/share/applications/arstack-studio.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=ARStack Studio
Comment=IEC 61850 process-bus engineering lab
Exec=$opt/ARStackStudio.AppImage
Terminal=false
Categories=Education;Science;Electronics;
DESKTOP
printf 'Installed ARStack Studio. Run: %s/arstack-studio\n' "$bin"
