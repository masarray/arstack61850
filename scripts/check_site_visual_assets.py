#!/usr/bin/env python3
"""Prevent invented landing branding: use ARStack's canonical Studio icon family.

All public site identity must be from assets/studio_icon; the hero image is the
actual Qt UI screenshot from assets/screenshots. Stdlib only, fast in CI.
"""
from __future__ import annotations

import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HTML = (ROOT / "index.html").read_text(encoding="utf-8")
ICON_ROOT = "assets/studio_icon"
REQUIRED_PNG = {
    f"{ICON_ROOT}/android-chrome-192x192.png": (192, 192),
    f"{ICON_ROOT}/android-chrome-512x512.png": (512, 512),
    f"{ICON_ROOT}/favicon-16x16.png": (16, 16),
    f"{ICON_ROOT}/favicon-32x32.png": (32, 32),
    "assets/screenshots/arstack-studio-windows.png": (960, 680),
}
OTHER = (
    f"{ICON_ROOT}/favicon.ico",
    f"{ICON_ROOT}/apple-touch-icon.png",
    f"{ICON_ROOT}/site.webmanifest",
)
LAB_PHOTO = "assets/photos/arstack-esp32-p4-lab-demo.webp"
errors: list[str] = []

for path, expected in REQUIRED_PNG.items():
    source = ROOT / path
    if not source.is_file() or source.stat().st_size < 100:
        errors.append(f"Missing or empty canonical product visual: {path}")
        continue
    header = source.read_bytes()[:24]
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        errors.append(f"Not a valid PNG header: {path}")
        continue
    size = struct.unpack(">II", header[16:24])
    if size != expected:
        errors.append(f"Wrong dimensions for {path}: {size} vs {expected}")

for path in OTHER:
    if not (ROOT / path).is_file() or (ROOT / path).stat().st_size == 0:
        errors.append(f"Missing canonical Studio identity asset: {path}")

# Verify that the contributed real board photo is a local, committed WebP.
photo_path = ROOT / LAB_PHOTO
if not photo_path.is_file() or photo_path.stat().st_size < 15000:
    errors.append(f"Missing or implausibly small hardware demo photo: {LAB_PHOTO}")
else:
    webp_header = photo_path.read_bytes()[:16]
    if webp_header[:4] != b"RIFF" or webp_header[8:12] != b"WEBP":
        errors.append(f"Hardware demo photo is not a WebP image: {LAB_PHOTO}")

required_html = [
    rf'<a class="brand"[^>]*>\s*<img src="{ICON_ROOT}/android-chrome-192x192.png"',
    r'<figure class="hero-visual" id="studio-preview">',
    r'<section class="wrap section" id="hardware-lab">',
    r'<img src="assets/photos/arstack-esp32-p4-lab-demo.webp"',
    r'<img src="assets/screenshots/arstack-studio-windows.png"',
    rf'<link rel="icon"[^>]*href="{ICON_ROOT}/favicon.ico"',
    rf'<link rel="icon"[^>]*href="{ICON_ROOT}/favicon-16x16.png"',
    rf'<link rel="icon"[^>]*href="{ICON_ROOT}/favicon-32x32.png"',
    rf'<link rel="apple-touch-icon" href="{ICON_ROOT}/apple-touch-icon.png"',
    rf'<link rel="manifest" href="{ICON_ROOT}/site.webmanifest"',
    rf'<meta property="og:image" content="https://masarray.github.io/arstack61850/{ICON_ROOT}/android-chrome-512x512.png">',
]
for pattern in required_html:
    if not re.search(pattern, HTML):
        errors.append(f"Missing original Studio identity reference: {pattern}")

for wrong in ('assets/favicon.ico', 'assets/favicon-96x96.png',
              'assets/web-app-manifest-', 'assets/site.webmanifest',
              'assets/apple-touch-icon.png', 'social-preview.svg',
              'class="monitor"', 'class="wave"', '<span class="mark"'):
    if wrong in HTML:
        errors.append(f"Non-Studio icon or invented UI: {wrong}")

if HTML.count('<img src="assets/screenshots/arstack-studio-windows.png"') != 1:
    errors.append("Native screenshot must appear exactly once")
if HTML.count('<img src="assets/photos/arstack-esp32-p4-lab-demo.webp"') != 1:
    errors.append("Hardware demonstration photo must appear exactly once")
if "https://www.waveshare.com/product/iot-communication/wired-comm-converter/esp32-p4-eth.htm" not in HTML:
    errors.append("Official global board listing link is missing")
if "https://docs.waveshare.com/ESP32-P4-ETH" not in HTML:
    errors.append("Official board documentation link is missing")

manifest_path = ROOT / ICON_ROOT / "site.webmanifest"
if manifest_path.is_file():
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("name") != "ARStack61850 — ARStack Studio":
            errors.append("Studio manifest must use the current product name")
        for icon in manifest.get("icons", []):
            icon_path = icon.get("src", "")
            if icon_path.startswith("/") or (ROOT / ICON_ROOT / icon_path).is_file() is False:
                errors.append(f"Studio manifest icon broken for project Pages: {icon_path}")
        if len(manifest.get("icons", [])) < 2:
            errors.append("Studio web manifest missing platform icon sizes")
    except (ValueError, OSError) as exc:
        errors.append(f"Invalid Studio manifest: {exc}")

capture = ROOT / ".github/workflows/public-studio-screenshot.yml"
if not capture.is_file() or "ARSTACK_STUDIO_QA_NATIVE_ONLY" not in capture.read_text(encoding="utf-8"):
    errors.append("Native screenshot capture provenance workflow is missing")

if errors:
    print("ARSTACK_PRODUCT_VISUAL_ASSET_GUARD_FAIL")
    for error in errors:
        print(" -", error)
    raise SystemExit(1)
print("ARSTACK_PRODUCT_VISUAL_ASSET_GUARD_PASS: only original Studio icon family, native screenshot and project-scoped paths")
