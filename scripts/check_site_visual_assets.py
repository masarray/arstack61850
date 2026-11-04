#!/usr/bin/env python3
"""Guard authenticity and deployability of the product landing's repository assets.

An accepted landing uses the project's own logo and native Qt screenshot, not
invented brand marks or hand-painted UI mockups. Stdlib only; fast documentation CI.
"""
from __future__ import annotations

import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HTML = (ROOT / "index.html").read_text(encoding="utf-8")

REQUIRED = {
    "assets/web-app-manifest-192x192.png": (192, 192),
    "assets/web-app-manifest-512x512.png": (512, 512),
    "assets/favicon-96x96.png": (96, 96),
    "assets/screenshots/arstack-studio-windows.png": (960, 680),
}
OTHER = (
    "assets/favicon.ico",
    "assets/apple-touch-icon.png",
    "assets/site.webmanifest",
)
errors: list[str] = []

for path, expected in REQUIRED.items():
    source = ROOT / path
    if not source.is_file() or source.stat().st_size < 15000:
        errors.append(f"Missing or implausibly small canonical image: {path}")
        continue
    header = source.read_bytes()[:24]
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        errors.append(f"Not a valid PNG header: {path}")
        continue
    size = struct.unpack(">II", header[16:24])
    if size != expected:
        errors.append(f"Unexpected image dimensions for {path}: {size} vs {expected}")

for path in OTHER:
    if not (ROOT / path).is_file() or (ROOT / path).stat().st_size == 0:
        errors.append(f"Missing required product identity asset: {path}")

required_html = [
    r'<a class="brand"[^>]*>\s*<img src="assets/web-app-manifest-192x192.png"',
    r'<figure class="hero-visual" id="studio-preview">',
    r'<img src="assets/screenshots/arstack-studio-windows.png"',
    r'<link rel="icon"[^>]*href="assets/favicon.ico"',
    r'<link rel="manifest" href="assets/site.webmanifest"',
    r'<meta property="og:image" content="https://masarray.github.io/arstack61850/assets/web-app-manifest-512x512.png">',
]
for pattern in required_html:
    if not re.search(pattern, HTML):
        errors.append(f"Missing canonical landing asset reference: {pattern}")
if HTML.count('<img src="assets/screenshots/arstack-studio-windows.png"') != 1:
    errors.append("The native screenshot must appear exactly once on the page")

for unverified in ('class="monitor"', 'class="wave"', '<span class="mark"', 'social-preview.svg'):
    if unverified in HTML:
        errors.append(f"Invented visual / unverified external identity reference: {unverified}")

capture = ROOT / ".github/workflows/public-studio-screenshot.yml"
if not capture.is_file() or "ARSTACK_STUDIO_QA_NATIVE_ONLY" not in capture.read_text(encoding="utf-8"):
    errors.append("Native Qt screenshot capture provenance workflow is missing")

if errors:
    print("ARSTACK_PRODUCT_VISUAL_ASSET_GUARD_FAIL")
    for error in errors:
        print(" -", error)
    raise SystemExit(1)
print("ARSTACK_PRODUCT_VISUAL_ASSET_GUARD_PASS: native screenshot, original brand and icons, project OG preview")
