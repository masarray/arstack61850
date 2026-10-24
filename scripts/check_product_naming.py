#!/usr/bin/env python3
"""Keep public product copy vendor-neutral and prevent legacy competitor naming in code."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
# Split literals so this policy file does not itself introduce indexed brand mentions.
PROHIBITED = (
    "ied" + "scout",
    "omi" + "cron",
    "dan" + "eo",
    "sipro" + "tec",
)
PUBLIC_PATHS = (
    "README.md",
    "apps/arstack_studio/README.md",
    "apps/ied_simulator/README.md",
    "docs/INSTALL.md",
    "docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md",
)
tracked = subprocess.run(
    ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True
).stdout.split(b"\0")
violations = []
for item in tracked:
    if not item:
        continue
    relative = item.decode("utf-8", errors="surrogateescape")
    lower_name = relative.casefold()
    if PROHIBITED[0] in lower_name:
        violations.append("filename: " + relative)
    if relative not in PUBLIC_PATHS:
        continue
    path = ROOT / relative
    if not path.is_file():
        continue
    data = path.read_text(encoding="utf-8", errors="replace").casefold()
    for term in PROHIBITED:
        if term in data:
            violations.append(f"public copy: {relative} contains restricted term")
if violations:
    sys.stderr.write("REPOSITORY_NAMING_GUARD_FAIL\n" + "\n".join(violations) + "\n")
    raise SystemExit(1)
print("REPOSITORY_NAMING_GUARD_PASS")
