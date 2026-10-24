#!/usr/bin/env python3
"""Keep user-facing ARStack copy consistent and vendor-neutral."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

# Keep competitor names out of product copy, without corrupting historical
# interoperability test fixtures and source-code regression evidence.
PROHIBITED = (
    "ied" + "scout",
    "omi" + "cron",
    "dan" + "eo",
    "sipro" + "tec",
)
PUBLIC_PATHS = (
    "README.md",
    "index.html",
    "CONTRIBUTING.md",
    "SUPPORT.md",
    "docs/GETTING_STARTED.md",
    "docs/INSTALL.md",
    "docs/COMMUNITY_TESTING.md",
    "docs/DEVELOPER_GUIDE.md",
    "apps/arstack_studio/README.md",
    "apps/ied_simulator/README.md",
    "docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md",
)

tracked = subprocess.run(
    ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True
).stdout.split(b"\0")
violations = []
tracked_paths = set()
for item in tracked:
    if not item:
        continue
    relative = item.decode("utf-8", errors="surrogateescape")
    tracked_paths.add(relative)
    lower_name = relative.casefold()
    if PROHIBITED[0] in lower_name:
        violations.append("filename: " + relative)
    if relative not in PUBLIC_PATHS and not relative.startswith(".github/ISSUE_TEMPLATE/"):
        continue
    path = ROOT / relative
    if not path.is_file():
        continue
    data = path.read_text(encoding="utf-8", errors="replace").casefold()
    for term in PROHIBITED:
        if term in data:
            violations.append(f"public copy: {relative} contains restricted term")

for required in ("docs/GETTING_STARTED.md", "docs/COMMUNITY_TESTING.md", "CONTRIBUTING.md", "SUPPORT.md"):
    if required not in tracked_paths:
        violations.append("missing public entrypoint: " + required)

if violations:
    sys.stderr.write("REPOSITORY_NAMING_GUARD_FAIL\n" + "\n".join(violations) + "\n")
    raise SystemExit(1)
print("REPOSITORY_NAMING_GUARD_PASS")
