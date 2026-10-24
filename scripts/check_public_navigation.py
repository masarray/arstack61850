#!/usr/bin/env python3
"""Validate local links in ARStack's public documentation and product landing."""
from __future__ import annotations
import html
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PAGES = [
    "README.md", "CONTRIBUTING.md", "SECURITY.md", "SUPPORT.md",
    "docs/INSTALL.md", "docs/GETTING_STARTED.md", "docs/COMMUNITY_TESTING.md",
    "docs/DEVELOPER_GUIDE.md", "docs/FEATURE_MATRIX.md",
    "apps/arstack_studio/README.md"
]
REQUIRED = {"README.md", "CONTRIBUTING.md", "SECURITY.md", "docs/INSTALL.md"}
MD_LINK = re.compile(r"!?\[[^\]\n]*\]\(([^)\n]+)\)")
HTML_LINK = re.compile(r"\b(?:href|src)=([\"'])(.*?)\1", re.I)


def heading_ids(text: str) -> set[str]:
    output: set[str] = set()
    seen: dict[str, int] = {}
    for line in text.splitlines():
        m = re.match(r"^#{1,6}\s+(.+?)\s*#*\s*$", line)
        if not m:
            continue
        h = re.sub(r"</?[^>]+>", "", m.group(1))
        h = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", h)
        h = re.sub(r"[\x60*_~]", "", h).strip().lower()
        slug = re.sub(r"[^\w -]", "", h, flags=re.UNICODE).replace(" ", "-")
        count = seen.get(slug, 0)
        output.add(slug if count == 0 else f"{slug}-{count}")
        seen[slug] = count + 1
    return output


def without_fences(text: str) -> str:
    lines = []
    fence = None
    for line in text.splitlines():
        marker = re.match(r"^\s*(\x60{3,}|~{3,})", line)
        if marker:
            if fence is None:
                fence = marker.group(1)[0]
            elif fence == marker.group(1)[0]:
                fence = None
        elif fence is None:
            lines.append(line)
    return "\n".join(lines)


def main() -> int:
    issues = []
    count = 0
    for rel in PAGES:
        origin = ROOT / rel
        if not origin.is_file():
            if rel in REQUIRED:
                issues.append(f"{rel}: required file missing")
            continue
        for hit in MD_LINK.finditer(without_fences(origin.read_text(encoding="utf-8"))):
            raw = hit.group(1).strip().split(None, 1)[0].strip("<>")
            url = urlsplit(html.unescape(raw))
            if not raw or url.scheme or url.netloc:
                continue
            count += 1
            target = (origin.parent / unquote(url.path)).resolve() if url.path else origin.resolve()
            if not target.is_relative_to(ROOT):
                issues.append(f"{rel}: link escapes repository: {raw}")
            elif not target.is_file():
                issues.append(f"{rel}: missing file: {raw}")
            elif url.fragment and unquote(url.fragment) not in heading_ids(target.read_text(encoding="utf-8")):
                issues.append(f"{rel}: missing heading: {raw}")
    landing = ROOT / "index.html"
    if landing.is_file():
        body = landing.read_text(encoding="utf-8")
        sections = set(re.findall(r'\bid=["\x27]([^"\x27]+)["\x27]', body))
        for match in HTML_LINK.finditer(body):
            raw = html.unescape(match.group(2).strip())
            url = urlsplit(raw)
            if not raw or url.scheme or url.netloc:
                continue
            count += 1
            if url.path:
                target = (ROOT / unquote(url.path)).resolve()
                if not target.is_relative_to(ROOT) or not target.is_file():
                    issues.append(f"index.html: missing local file {raw}")
            if url.fragment and unquote(url.fragment) not in sections:
                issues.append(f"index.html: missing section {raw}")
    if issues:
        print(f"FAIL: {len(issues)} invalid public navigation links")
        for issue in issues:
            print(" -", issue)
        return 1
    print(f"PASS: {count} local links and section references across public guides and landing.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
