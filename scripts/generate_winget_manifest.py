#!/usr/bin/env python3
"""Prepare WinGet manifests from immutable GitHub Release metadata (not an upstream submission)."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.request import Request, urlopen

REPO="masarray/arstack61850"
PKG="MasArray.ARStackStudio"
def fetch_json(url):
    with urlopen(Request(url,headers={"User-Agent":"arstack-winget-manifest",
                                       "Accept":"application/vnd.github+json"}),timeout=30) as rsp:
        return json.load(rsp)
def fetch_bytes(url):
    with urlopen(Request(url,headers={"User-Agent":"arstack-winget-manifest"}),timeout=90) as rsp:
        return rsp.read()
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--version",required=True,help="Unique WinGet SemVer release version, e.g. 0.2.0")
    p.add_argument("--tag",help="GitHub release tag; default newest stable")
    p.add_argument("--installer-type",choices=("inno","nullsoft"),required=True)
    p.add_argument("--out",default="dist/winget")
    args=p.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:\.\d+)?",args.version):
        p.error("--version must be numeric X.Y.Z or X.Y.Z.W")
    path=("tags/"+args.tag) if args.tag else "latest"
    r=fetch_json(f"https://api.github.com/repos/{REPO}/releases/{path}")
    if r.get("draft") or r.get("prerelease"):
        p.error("Only a published stable release can be packaged for WinGet")
    matches=[a for a in r.get("assets",[]) if re.search(r"(win|windows).*setup\.exe$",a["name"],re.I)]
    if len(matches)!=1:
        p.error("Expected exactly one Windows Setup installer in the release")
    asset=matches[0]
    digest=asset.get("digest") or ""
    if re.fullmatch(r"sha256:[0-9a-fA-F]{64}",digest):
        sha=digest.split(":")[1].upper()
    else:
        sums=next((a for a in r["assets"] if a["name"]=="SHA256SUMS.txt"),None)
        if not sums:
            p.error("Published installer checksum missing")
        content=fetch_bytes(sums["browser_download_url"]).decode("ascii")
        found=re.search(r"^([0-9a-fA-F]{64})\s+\*?"+re.escape(asset["name"])+r"\s*$",content,re.M)
        if not found:
            p.error("Installer entry not found in release SHA256SUMS.txt")
        sha=found.group(1).upper()
    # The upstream WinGet CI downloads and validates InstallerSha256 independently.
    out=Path(args.out)/args.version
    out.mkdir(parents=True,exist_ok=True)
    installer=f"""# yaml-language-server: $schema=https://aka.ms/winget-manifest.installer.1.9.0.schema.json
PackageIdentifier: {PKG}
PackageVersion: {args.version}
InstallerType: {args.installer_type}
Installers:
  - Architecture: x64
    InstallerUrl: {asset['browser_download_url']}
    InstallerSha256: {sha}
ManifestType: installer
ManifestVersion: 1.9.0
"""
    version=f"""# yaml-language-server: $schema=https://aka.ms/winget-manifest.version.1.9.0.schema.json
PackageIdentifier: {PKG}
PackageVersion: {args.version}
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.9.0
"""
    locale=f"""# yaml-language-server: $schema=https://aka.ms/winget-manifest.defaultLocale.1.9.0.schema.json
PackageIdentifier: {PKG}
PackageVersion: {args.version}
PackageLocale: en-US
Publisher: MasArray
PackageName: ARStack Studio
License: GPL-3.0-only
ShortDescription: Open-source IEC 61850 process-bus engineering laboratory
PackageUrl: https://github.com/{REPO}
ManifestType: defaultLocale
ManifestVersion: 1.9.0
"""
    for suffix,content in (("installer",installer),("version",version),("locale.en-US",locale)):
        (out/f"{PKG}.{suffix}.yaml").write_text(content,encoding="utf-8")
    print(f"WinGet manifests for {r['tag_name']}: {out}; review and submit upstream before advertising winget install")
if __name__=="__main__":
    main()
