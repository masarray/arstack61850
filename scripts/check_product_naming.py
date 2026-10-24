#!/usr/bin/env python3
"""Keep product copy, source comments, examples and workflows vendor-neutral."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
# Split tokens to avoid introducing indexed commercial names in this check itself.
TERMS = (
    "ied" + "scout",
    "omi" + "cron",
    "dan" + "eo",
    "sipro" + "tec",
    "sie" + "mens",
    "hita" + "chi",
    "ab" + "b",
)
# Word boundaries protect unrelated identifiers (e.g. 'abbreviated').
PATTERN = re.compile(
    r"(?<![a-z0-9])(?:" + "|".join(re.escape(t) for t in TERMS) + r")(?![a-z0-9])",
    re.IGNORECASE,
)
TEXT_EXTENSIONS = {
    ".md", ".txt", ".json", ".yaml", ".yml", ".qml", ".cpp", ".hpp",
    ".h", ".c", ".cc", ".py", ".js", ".ts", ".html", ".css", ".ps1",
    ".sh", ".cmake", ".xml", ".iss", ".nsi",
}
tracked = subprocess.run(
    ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True
).stdout.split(b"\0")
violations = []
scanned = 0
for item in tracked:
    if not item:
        continue
    relative = item.decode("utf-8", errors="surrogateescape")
    path = ROOT / relative
    for term in TERMS:
        if PATTERN.fullmatch(path.name.casefold()):
            violations.append(f"filename: {relative}")
    if not path.is_file() or path.suffix.casefold() not in TEXT_EXTENSIONS:
        continue
    scanned += 1
    contents = path.read_text(encoding="utf-8", errors="replace")
    for number, line in enumerate(contents.splitlines(), start=1):
        match = PATTERN.search(line)
        if match:
            violations.append(
                f"{relative}:{number}: public source contains restricted commercial naming"
            )
if violations:
    print("REPOSITORY_NAMING_GUARD_FAIL", file=sys.stderr)
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)
print(f"REPOSITORY_NAMING_GUARD_PASS files_scanned={scanned}")
