#!/usr/bin/env python3
"""Promote user-accepted Process Bus Simulator RC1 to stable packaging.

The Studio executable, Linux AppImage, and both ESP32-P4 firmware images are
copied from immutable SHA-pinned RC1 assets. No application/firmware rebuilds.
The Windows Inno stable installer is compiled separately by GitHub Actions.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import zipfile

RC_TAG = "processbus-sim-dual-rc1-20261105"
STABLE_TAG = "processbus-sim-dual-stable-20261105"
SOURCE_SHA = "ab646b58da601569c956f2d19ff45100129f19a0"
MERGED_SHA = "525902548504aa997f868c43f51208fb13beecaf"
EXPECTED = {
    "ARStack-Studio-Preview-win-x64-portable.zip": "47d9581b8d94edc8c75dcaa99d5623cf7f9f9a9d018970cb394962d7ca2270de",
    "ARStack-Studio-linux-x86_64.AppImage": "36005a79d01470f5bd0f4667cc883a107927562854756c9085427fff4949c17f",
    "arstack-esp32p4-smv-0.1.1.bin": "39963b6e531ada33a912b6e6a08915e038d4d6d19dcf8a4354756d5349d94091",
    "arstack-esp32p4-smv-0.1.1-v3x.bin": "a7628f912dcf749d0591ffdb5410e50c82864b61b3b25fa5e1cdb00be9754a28",
    "firmware-catalog.json": "cbb43524e0eeb54eab8fa57dd1606fa885e7ec946bcc415ca0b0491c7025096b",
    "firmware-manifest.json": "f624aeb0efd72b0813503abf267c62edc689c47357323d91be40fe031ade8b83",
}
STABLE_NOTES = """# ARStack Process Bus Simulator - Dual Firmware Stable

Public stable release promoted from operator-tested RC1.

**Preferred Windows download: ARStack-Studio-win-x64-setup.zip.**
Extract the ZIP and run the contained ARStack-Studio-win-x64-setup.exe.
Also available: a direct Setup EXE, Windows Portable ZIP, and Linux AppImage.

The executable for Studio, Linux AppImage, and both ESP32-P4 images are
**bit-identical to the tested RC1**. Only Windows stable-channel installer
packaging and release metadata changed. This is not a new firmware build.

Supports the existing IEC 61850 4I+4V SV simulation, SCL engineering profiles,
and GPS-style PTP Grandmaster simulation. The dual firmware bundle includes
separate pre-v3 and v3.x images with a verified revision-aware catalog.
The M4 three-MU software controls are integrated. Sustained 3x4000fps
field timing acceptance remains separate from the operator-tested RC1 release.
**The v3.x firmware image is distributed as an ENGINEERING PREVIEW within this
stable Studio package**: v3.x board/PHY boot, SV/PTP Ethernet capture, recovery,
autonomous operation and broad hardware compatibility are still undergoing
physical acceptance. Existing pre-v3 test evidence must not be presented as
v3.x evidence. The revision-aware catalog retains its original preview channel.

The Windows stable Inno installer has its own AppId; previously installed
Preview builds can be uninstalled separately after confirming stable works.
ZIP does not eliminate Windows SmartScreen/antivirus inspection of unsigned EXEs.

Use on an isolated laboratory process bus, never an energized protection system.
Historical M3.H.3 stable release and the user-tested RC1 are preserved.
"""

def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def require(condition, message):
    if not condition:
        raise ValueError(message)

def prepare(rc, release_meta, stage, out):
    meta = json.loads(release_meta.read_text(encoding="utf-8-sig"))
    require(meta["tag_name"] == RC_TAG and meta["prerelease"] and not meta["draft"], "Wrong RC release")
    assets = {a["name"]: a for a in meta["assets"]}
    for name, expected in EXPECTED.items():
        file = rc / name
        require(file.is_file() and digest(file) == expected, "Pinned asset changed: " + name)
        require(assets[name].get("digest") == "sha256:" + expected, "Release registry digest mismatch: " + name)
    original = json.loads((rc / "release-provenance.json").read_text(encoding="utf-8-sig"))
    require(original["sourceHead"] == SOURCE_SHA and original["mergedMain"] == MERGED_SHA and original["sourcePR"] == 359, "Wrong RC provenance")
    catalog = json.loads((rc / "firmware-catalog.json").read_text(encoding="utf-8-sig"))
    manifest = json.loads((rc / "firmware-manifest.json").read_text(encoding="utf-8-sig"))
    require(catalog["sourceCommit"] == manifest["sourceCommit"] == SOURCE_SHA, "Firmware/Studio revision drift")
    require(manifest["bootSimulatedGlobalGmEnabled"] and manifest["phaseSyncPreview"], "Lost simulated GM")
    require({i["policy"] for i in catalog["images"]} == {"pre-v3", "v3.x"} and len(catalog["images"]) == 2, "Must provide exactly two revision images")
    for entry in catalog["images"]:
        require(digest(rc / entry["image"]) == entry["sha256"], "Corrupt revision image")
    stage.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(rc / "ARStack-Studio-Preview-win-x64-portable.zip") as archive:
        for entry in archive.infolist():
            p = pathlib.PurePosixPath(entry.filename)
            require(not p.is_absolute() and ".." not in p.parts and not entry.filename.startswith("\\"), "Unsafe ZIP entry")
        archive.extractall(stage)
    for rel in ("ARStackStudio.exe", "ARStackStudio.ico", "tools/espflash.exe",
                "firmware/firmware-manifest.json", "firmware/firmware-catalog.json"):
        require((stage / rel).is_file(), "Missing portable file: " + rel)
    require((stage / "firmware/firmware-catalog.json").read_bytes() == (rc / "firmware-catalog.json").read_bytes(), "Packaged catalog differs")
    for entry in catalog["images"]:
        require(digest(stage / "firmware" / entry["image"]) == entry["sha256"], "Portable firmware mismatch")
    (stage / "PREVIEW-PROVENANCE.json").unlink(missing_ok=True)
    (stage / "RELEASE-PROVENANCE.json").write_text(json.dumps({
        "schema": "arstack.studio.release-provenance.v1",
        "channel": "stable", "sourceRelease": RC_TAG,
        "studioFirmwareSource": SOURCE_SHA, "sourceMain": MERGED_SHA,
        "studioExeSha256": digest(stage / "ARStackStudio.exe"),
        "firmwareCatalogPreserved": True
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    with zipfile.ZipFile(out / "ARStack-Studio-win-x64-portable.zip", "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for f in sorted(stage.rglob("*")):
            if f.is_file():
                archive.write(f, f.relative_to(stage).as_posix())
    for filename in ("ARStack-Studio-linux-x86_64.AppImage",
                     "ARStack-Studio-linux-x86_64.sha256",
                     "arstack-esp32p4-smv-0.1.1.bin",
                     "arstack-esp32p4-smv-0.1.1-v3x.bin",
                     "firmware-manifest.json", "firmware-catalog.json"):
        shutil.copy2(rc / filename, out / filename)
    require(digest(out / "ARStack-Studio-linux-x86_64.AppImage") == EXPECTED["ARStack-Studio-linux-x86_64.AppImage"], "Linux binary drift")
    print("RC1 EXACT-BINARY DUAL-SILICON STABLE STAGING: PASS")

def finish(stage, out):
    setup = out / "ARStack-Studio-win-x64-setup.exe"
    require(setup.is_file(), "Stable Inno Setup missing")
    zpath = out / "ARStack-Studio-win-x64-setup.zip"
    with zipfile.ZipFile(zpath, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.write(setup, setup.name)
    with zipfile.ZipFile(zpath) as archive:
        require(archive.namelist() == [setup.name], "Setup ZIP must contain one installer")
        require(hashlib.sha256(archive.read(setup.name)).hexdigest() == digest(setup), "Setup ZIP checksum mismatch")
    (out / "RELEASE_NOTES.md").write_text(STABLE_NOTES, encoding="utf-8")
    (out / "release-provenance.json").write_text(json.dumps({
        "schema": "arstack.public-release.provenance.v1",
        "channel": "stable", "tag": STABLE_TAG, "derivedFrom": RC_TAG,
        "sourceHead": SOURCE_SHA, "mergedMain": MERGED_SHA,
        "applicationFirmwareRecompiled": False, "stableInstallerRepackaged": True,
        "windowsPreferredAsset": zpath.name,
        "firmwarePolicies": ["pre-v3", "v3.x"],
        "v3xHardwareQualification": "engineering-preview; physical acceptance pending"
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    files = sorted(x for x in out.iterdir() if x.is_file() and x.name not in ("RELEASE_NOTES.md", "SHA256SUMS.txt"))
    require(len(files) >= 10, "Stable release missing assets")
    sums = "".join(digest(f) + "  " + f.name + "\n" for f in files)
    (out / "SHA256SUMS.txt").write_text(sums, encoding="ascii")
    for f in files:
        require(digest(f) in sums, "Checksum instability")
    require(digest(stage / "ARStackStudio.exe") == json.loads((stage / "RELEASE-PROVENANCE.json").read_text())["studioExeSha256"], "Stable Studio binary drift")
    print("STABLE INSTALLER ZIP AND ALL ASSET SHA256: PASS")
    for f in sorted(out.iterdir()):
        print(f.name, f.stat().st_size)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_action", choices=("prepare", "finish"))
    ap.add_argument("--rc", type=pathlib.Path, required=True)
    ap.add_argument("--release-meta", type=pathlib.Path, required=True)
    ap.add_argument("--stage", type=pathlib.Path, required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()
    if args.stage_action == "prepare":
        prepare(args.rc, args.release_meta, args.stage, args.out)
    else:
        finish(args.stage, args.out)

if __name__ == "__main__":
    main()
