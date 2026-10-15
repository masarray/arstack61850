#!/usr/bin/env python3
"""Fail CI when a prohibited product name enters tracked source or paths."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROHIBITED = ("ied" + "scout").encode("ascii")
tracked = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT,
                         capture_output=True, check=True).stdout.split(b"\0")
violations = []
for item in tracked:
    if not item:
        continue
    relative = item.decode("utf-8", errors="surrogateescape")
    if PROHIBITED in item.lower():
        violations.append("filename: " + relative)
    path = ROOT / relative
    if not path.is_file():
        continue
    with path.open("rb") as handle:
        prefix = handle.read(4096)
        if b"\0" in prefix:
            continue
        content = prefix + handle.read()
    if PROHIBITED in content.lower():
        violations.append("text: " + relative)
if violations:
    sys.stderr.write("REPOSITORY_NAMING_GUARD_FAIL\n" + "\n".join(violations) + "\n")
    raise SystemExit(1)
print("REPOSITORY_NAMING_GUARD_PASS")
