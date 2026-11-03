#!/usr/bin/env python3
"""Fail-closed CI/package validation for ESP32-P4 silicon revision artifacts.

The Studio runtime separately validates the catalog and SHA before a write.
This check prevents shipping a catalog with a wrong bootloader image header.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

CAP = 16 * 1024 * 1024
SCHEMA = "arstack.studio.firmware.catalog.v2"


def header_revision_bounds(image: bytes) -> tuple[int, int]:
    # ESP-IDF esp_image_header_t: min_chip_rev_full @15, max @17,
    # little-endian; merged raw images contain the bootloader at offset 0.
    if len(image) < 24 or image[0] != 0xE9:
        raise ValueError("missing ESP image bootloader header at offset zero")
    minimum = int.from_bytes(image[15:17], "little")
    maximum = int.from_bytes(image[17:19], "little")
    if minimum > maximum:
        raise ValueError("inverted chip revision bounds in bootloader")
    return minimum, maximum


def verify(root: Path) -> None:
    root = root.resolve(strict=True)
    legacy = json.loads((root / "firmware-manifest.json").read_text())
    catalog = json.loads((root / "firmware-catalog.json").read_text())
    if (catalog.get("schema") != SCHEMA or catalog.get("chip") != "esp32p4"
            or legacy.get("chip") != "esp32p4"
            or catalog.get("sourceCommit") != legacy.get("sourceCommit")
            or catalog.get("version") != legacy.get("version")
            or catalog.get("protocol") != legacy.get("protocol")):
        raise ValueError("catalog identity mismatch")
    images = catalog.get("images")
    if not isinstance(images, list) or len(images) != 2:
        raise ValueError("catalog must include exactly two images")
    policies = set()
    names = set()
    for entry in images:
        if not isinstance(entry, dict):
            raise ValueError("invalid image entry")
        policy = entry.get("policy")
        name = entry.get("image")
        sha = entry.get("sha256")
        if (policy not in ("pre-v3", "v3.x")
                or not isinstance(name, str) or Path(name).name != name
                or name in names or policy in policies
                or not isinstance(sha, str) or len(sha) != 64):
            raise ValueError("unsafe image or duplicate revision selection")
        policies.add(policy)
        names.add(name)
        low, high = (100, 199) if policy == "pre-v3" else (302, 302)
        if (entry.get("minRevision"), entry.get("maxRevision")) != (low, high):
            raise ValueError("unexpected qualification range")
        path = root / name
        if path.is_symlink() or not path.is_file() or path.resolve().parent != root:
            raise ValueError("unsafe image path")
        if not (24 <= path.stat().st_size <= CAP):
            raise ValueError("invalid image size")
        payload = path.read_bytes()
        if hashlib.sha256(payload).hexdigest() != sha:
            raise ValueError("image SHA256 mismatch")
        if catalog["sourceCommit"][:16].encode("ascii") not in payload:
            raise ValueError("image does not identify the exact source commit")
        minimum, maximum = header_revision_bounds(payload)
        if not (minimum <= low <= high <= maximum):
            raise ValueError(f"{policy}: bootloader incompatible with approved revision {low}..{high} (header {minimum}..{maximum})")
        if policy == "pre-v3" and (name != legacy["image"] or sha != legacy["sha256"] or maximum >= 300):
            raise ValueError("legacy header incorrectly enables v3 silicon")
        if policy == "v3.x" and minimum < 300:
            raise ValueError("v3 bootloader incorrectly enables pre-v3 silicon")
    if policies != {"pre-v3", "v3.x"}:
        raise ValueError("missing silicon variant")
    print("Revision-aware firmware package: PASS (two disjoint images, headers, source and SHA)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_directory", type=Path)
    arguments = parser.parse_args()
    verify(arguments.firmware_directory)
