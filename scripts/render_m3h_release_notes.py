#!/usr/bin/env python3
"""Render the M3.H public release document into portable GitHub Release notes."""
from pathlib import Path
import posixpath
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md"
REPO = "masarray/arstack61850"
BASE = "docs/releases"

def normalize(match):
    prefix, relative, suffix = match.groups()
    target = relative.split("#", 1)
    path = posixpath.normpath(posixpath.join(BASE, target[0]))
    if path.startswith("../") or path == "..":
        raise ValueError(f"Relative path escapes repository: {relative}")
    anchor = "#" + target[1] if len(target) > 1 else ""
    if not (ROOT / path).is_file():
        raise FileNotFoundError(f"Broken release-notes link: {relative} → {path}")
    if prefix.startswith("!["):
        url = f"https://raw.githubusercontent.com/{REPO}/main/{path}{anchor}"
    else:
        url = f"https://github.com/{REPO}/blob/main/{path}{anchor}"
    return prefix + url + suffix

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: render_m3h_release_notes.py output.md")
    source = SOURCE.read_text(encoding="utf-8")
    for required in (
        "163,048", "3,999.884", "smpSynch=2",
        "a922e9bdba4e57a4e88406eb83344455dd8ff142",
        "4902e8bf44e78fc55a139ce84a1273f0768ef6fe10000f5d0f85d75d2f6c736e",
        "ca01424e4102aa65e353737024ebf3258b4d2f614fa60d932c95c353ee228d8f",
        "61c508e9bf1357eb964d2b817b260b6a0e985446ddd68de1d45d9467e25c4607",
    ):
        if required not in source:
            raise ValueError(f"Missing immutable release evidence: {required}")
    rendered = re.sub(r"(!?\[[^\]]+\]\()(\.\.?/[^)\s]+)(\))", normalize, source)
    if re.search(r"!?\[[^\]]+\]\(\.\.?/", rendered):
        raise ValueError("Unresolved relative release link")
    dest = Path(sys.argv[1])
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(rendered, encoding="utf-8")
    print(f"PASS: verified and rendered {dest}, {len(rendered)} characters")

if __name__ == "__main__":
    main()
