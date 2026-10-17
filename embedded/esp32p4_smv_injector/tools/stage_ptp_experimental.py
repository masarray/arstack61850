#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage an opt-in, non-release ESP32-P4 PTP firmware test artifact."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in {"source", "receiver"}:
        raise SystemExit("usage: stage_ptp_experimental.py source|receiver DEST_ROOT")
    role = sys.argv[1]
    destination = Path(sys.argv[2]).resolve() / role
    root = Path.cwd().resolve()
    build = root / "build"
    head = os.environ.get("ARSTACK_SOURCE_COMMIT", "")
    if not re.fullmatch(r"[0-9a-f]{40}", head):
        raise SystemExit("require exact 40-character GitHub PR head SHA")
    config_path = root / "sdkconfig"
    config = config_path.read_text(encoding="utf-8")
    expected = {
        "CONFIG_IDF_TARGET": '"esp32p4"',
        "CONFIG_AR_PTP_OPERATING_ROLE": "0" if role == "source" else "1",
        "CONFIG_AR_PTP_LAB_TX": "y",
        "CONFIG_AR_SMV_LIVE_TX": "y",
        "CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL": "y",
        "CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL": "y",
        "CONFIG_ETH_TRANSMIT_MUTEX": "y",
    }
    for key, value in expected.items():
        if f"{key}={value}" not in config.splitlines():
            raise SystemExit(f"unsafe {role} artifact: missing {key}={value}")
    if "# CONFIG_AR_SMV_BROADCAST_MIRROR is not set" not in config:
        raise SystemExit("diagnostic broadcast mirror must be OFF")

    flasher_file = build / "flasher_args.json"
    flasher = json.loads(flasher_file.read_text(encoding="utf-8"))
    flash_files = flasher.get("flash_files")
    if not isinstance(flash_files, dict) or len(flash_files) < 3:
        raise SystemExit("missing complete ESP-IDF flasher_args flash_files")
    image_names = [str(x) for x in flash_files.values()]
    if not all(name.endswith(".bin") for name in image_names):
        raise SystemExit("flash_files contains non-binary image")
    if not any("bootloader" in name for name in image_names) or not any(
        "partition" in name for name in image_names
    ) or not any("/" not in name for name in image_names):
        raise SystemExit("bootloader, partition table, or app image missing")

    destination.mkdir(parents=True, exist_ok=False)
    shutil.copy2(config_path, destination / "sdkconfig")
    shutil.copy2(flasher_file, destination / "flasher_args.json")
    images = []
    for offset, name in sorted(flash_files.items(), key=lambda x: int(x[0], 0)):
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise SystemExit(f"invalid flash image path {name!r}")
        source = (build / relative).resolve()
        if not source.is_relative_to(build) or not source.is_file() or source.stat().st_size == 0:
            raise SystemExit(f"missing or unsafe flash image {name!r}")
        target = destination / "build" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        images.append({
            "flashOffset": offset,
            "relativeFile": str(Path("build") / relative),
            "bytes": source.stat().st_size,
            "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        })

    manifest = {
        "kind": "ARStack61850 experimental PTP bench-only; NOT a stable release",
        "boardTarget": "esp32p4",
        "role": role,
        "prHeadCommit": head,
        "checkoutCommit": os.environ.get("GITHUB_SHA", ""),
        "semanticBuildId": head[:16],
        "idfImageSource": "build/flasher_args.json",
        "smpSynchAutoQualification": False,
        "hardwareValidated": False,
        "flashImages": images,
        "sdkconfigSha256": hashlib.sha256(config_path.read_bytes()).hexdigest(),
    }
    (destination / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"Experimental {role}: {len(images)} verified flash images, PR head {head}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
