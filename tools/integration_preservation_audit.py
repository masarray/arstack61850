#!/usr/bin/env python3
"""Offline, read-only ARStack PR preservation audit.

This checks a PINNED GitHub inventory; it does not fetch, merge, close or mutate
pull requests. It never treats blob equality as complete functional equivalence.
Run from a checkout containing PR head/base Git objects for full ancestry audit.

    python3 tools/integration_preservation_audit.py --verify-only
    python3 tools/integration_preservation_audit.py --scan-git --target HEAD \
        --report /tmp/arstack-preservation-report.json

Use "git fetch origin <sha>" to retrieve missing public commit objects; absence
from a shallow checkout must NEVER be reported as work lost.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

DEFAULT_SNAPSHOT = "docs/integration/PR_PRESERVATION_SNAPSHOT_2026-10-22.json"
SIDE_GLOB = "PR_SIDE_BLOB_AUDIT_GROUP_*_2026-10-22.json"
INDEPENDENT_GLOB = "PR_INDEPENDENT_SOURCE_GROUP_*_2026-10-22.json"


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected JSON object")
    return value


def load_and_validate(
    root: Path,
) -> tuple[dict[str, Any], dict[int, dict[str, Any]], dict[int, dict[str, Any]]]:
    manifest = read_json(root / DEFAULT_SNAPSHOT)
    if manifest.get("schema") != "arstack61850.pr-preservation-snapshot.v1":
        raise ValueError("Unsupported preservation manifest schema")
    pull_requests = manifest["pull_requests"]
    issues = manifest["issues"]
    counts = manifest["inventory"]
    numbers = [p["number"] for p in pull_requests]
    if len(numbers) != len(set(numbers)):
        raise ValueError("Duplicate PR number in preserved inventory")
    if len({x["number"] for x in issues}) != len(issues):
        raise ValueError("Duplicate issue number in preserved inventory")
    if len(numbers) != counts["open_prs"] or len(issues) != counts["open_issues"]:
        raise ValueError("PR or issue record count differs from snapshot header")
    by_number = {p["number"]: p for p in pull_requests}
    categories = {
        "CANONICAL_CHAIN": set(manifest["direct_chain_numbers_oldest_to_newest"]),
        "SIDE_BRANCH": set(manifest["side_numbers"]),
        "INDEPENDENT_LANE": set(manifest["independent_numbers"]),
    }
    if set.union(*categories.values()) != set(numbers):
        raise ValueError("Missing or extra PR classification")
    if sum(len(v) for v in categories.values()) != len(numbers):
        raise ValueError("PR classifications overlap")
    for category, ids in categories.items():
        if counts[{
            "CANONICAL_CHAIN": "canonical_chain",
            "SIDE_BRANCH": "side",
            "INDEPENDENT_LANE": "independent",
        }[category]] != len(ids):
            raise ValueError(f"{category}: count mismatch")
        if any(by_number[n]["classification"] != category for n in ids):
            raise ValueError(f"{category}: per-PR classification mismatch")
    for p in pull_requests:
        for field in ("head_sha", "base_sha"):
            sha = p[field]
            if not isinstance(sha, str) or len(sha) != 40:
                raise ValueError(f"PR #{p['number']}: malformed {field}")

    audits: dict[int, dict[str, Any]] = {}
    filenames = sorted((root / "docs/integration").glob(SIDE_GLOB))
    if len(filenames) != 3:
        raise ValueError(f"Expected three side-branch audit groups; got {len(filenames)}")
    for file in filenames:
        group = read_json(file)
        if group.get("schema") != "arstack61850.side-file-blob-audit.v1":
            raise ValueError(f"{file}: invalid group schema")
        for record in group["records"]:
            number = record["number"]
            if number in audits or number not in categories["SIDE_BRANCH"]:
                raise ValueError(f"Duplicate or unexpected side PR #{number}")
            if record.get("error"):
                raise ValueError(f"PR #{number} source blob inspection failed: {record['error']}")
            if record["source_sha"] != by_number[number]["head_sha"]:
                raise ValueError(f"PR #{number} source SHA does not match frozen inventory")
            if record["source_changed_files_count"] != len(record["files"]):
                raise ValueError(f"PR #{number}: changed-file count mismatch")
            for f in record["files"]:
                if f["status"] == "EXACT_BLOB_MATCH" and (
                    f["source_blob_sha"] != f["tip_blob_sha"]
                ):
                    raise ValueError(f"PR #{number}: invalid exact blob status")
                if f["status"] == "MISSING_IN_TIP_REVIEW" and (
                    not f["source_blob_sha"] or f["tip_blob_sha"]
                ):
                    raise ValueError(f"PR #{number}: invalid missing-file status")
            audits[number] = record
    if set(audits) != categories["SIDE_BRANCH"]:
        raise ValueError("Some side PRs have no frozen source blob audit")

    independent: dict[int, dict[str, Any]] = {}
    independent_files = sorted((root / "docs/integration").glob(INDEPENDENT_GLOB))
    if len(independent_files) != 3:
        raise ValueError(
            f"Expected three independent source groups; got {len(independent_files)}"
        )
    for file in independent_files:
        group = read_json(file)
        if group.get("schema") != "arstack61850.independent-pr-retention.v1":
            raise ValueError(f"{file}: invalid independent schema")
        for record in group["records"]:
            number = record["number"]
            if number in independent or number not in categories["INDEPENDENT_LANE"]:
                raise ValueError(f"Duplicate or unexpected independent PR #{number}")
            if record.get("error"):
                raise ValueError(f"PR #{number} source inventory error: {record['error']}")
            if record["head_sha"] != by_number[number]["head_sha"]:
                raise ValueError(f"PR #{number} source SHA drift in independent inventory")
            if record["changed_file_count"] != len(record["changed_paths"]):
                raise ValueError(f"PR #{number} independent path count mismatch")
            independent[number] = record
    if set(independent) != categories["INDEPENDENT_LANE"]:
        raise ValueError("Independent-lane coverage is incomplete")
    return manifest, audits, independent


def git(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        check=False,
    )


def commit_exists(repo: Path, sha: str) -> bool:
    return git(repo, "cat-file", "-e", f"{sha}^{{commit}}").returncode == 0


def blob_at(repo: Path, sha: str, path: str) -> str | None:
    result = git(repo, "rev-parse", "-q", "--verify", f"{sha}:{path}")
    return result.stdout.strip() if result.returncode == 0 else None


def audit_git(
    root: Path, manifest: dict[str, Any], audits: dict[int, dict[str, Any]],
    independent: dict[int, dict[str, Any]], target_ref: str,
) -> dict[str, Any]:
    resolved = git(root, "rev-parse", "--verify", f"{target_ref}^{{commit}}")
    if resolved.returncode != 0:
        raise ValueError(f"Target ref '{target_ref}' is not in the checkout")
    target = resolved.stdout.strip()
    records = []
    absent = []
    for pr in sorted(manifest["pull_requests"], key=lambda p: p["number"], reverse=True):
        number = pr["number"]
        head = pr["head_sha"]
        base = pr["base_sha"]
        status: dict[str, Any] = {
            "number": number,
            "lane": pr["lane"],
            "classification": pr["classification"],
            "head_sha": head,
            "head_available": commit_exists(root, head),
            "base_available": commit_exists(root, base),
        }
        if not status["head_available"]:
            status["ancestry"] = "NOT_FETCHED_NO_CONCLUSION"
            absent.append(number)
        else:
            ancestor = git(root, "merge-base", "--is-ancestor", head, target)
            if ancestor.returncode not in (0, 1):
                raise RuntimeError(f"Unable to test ancestry of PR #{number}")
            status["ancestry"] = (
                "COMMIT_IS_ANCESTOR" if ancestor.returncode == 0
                else "COMMIT_NOT_ANCESTOR_REVIEW"
            )
            if ancestor.returncode != 0:
                delta = git(root, "rev-list", "--count", f"{target}..{head}")
                if delta.returncode == 0:
                    status["source_only_commits"] = int(delta.stdout.strip())
        if number in audits:
            source = audits[number]
            results = []
            for item in source["files"]:
                path = item["path"]
                if not status["head_available"]:
                    results.append({"path": path, "status": "NOT_FETCHED_NO_CONCLUSION"})
                    continue
                old_blob = blob_at(root, head, path)
                new_blob = blob_at(root, target, path)
                if old_blob is None:
                    verdict = "SOURCE_DELETED_OR_RENAMED"
                elif new_blob is None:
                    verdict = "MISSING_FROM_TIP_REVIEW"
                elif old_blob == new_blob:
                    verdict = "EXACT_BLOB_ONLY"
                else:
                    verdict = "DIFFERENT_BLOB_SEMANTIC_REVIEW"
                results.append({
                    "path": path,
                    "status": verdict,
                    "source_blob": old_blob,
                    "target_blob": new_blob,
                })
            status["side_changed_paths"] = results
        if number in independent:
            independent_results = []
            for item in independent[number]["changed_paths"]:
                path = item["path"]
                if not status["head_available"]:
                    independent_results.append({
                        "path": path, "status": "NOT_FETCHED_NO_CONCLUSION"
                    })
                    continue
                old_blob = blob_at(root, head, path)
                new_blob = blob_at(root, target, path)
                if old_blob is None:
                    verdict = "SOURCE_DELETED_OR_RENAMED"
                elif new_blob is None:
                    verdict = "INDEPENDENT_SOURCE_FILE_ABSENT_FROM_PTP"
                elif old_blob == new_blob:
                    verdict = "EXACT_BLOB_ONLY"
                else:
                    verdict = "INDEPENDENT_DIFFERENT_BLOB_REVIEW_IN_OWN_LANE"
                independent_results.append({
                    "path": path, "status": verdict,
                    "source_blob": old_blob, "target_blob": new_blob,
                })
            status["independent_changed_paths"] = independent_results
        records.append(status)
    return {
        "schema": "arstack61850.pr-git-preservation-result.v1",
        "snapshot_date": manifest["captured_date"],
        "repo": manifest["repo"],
        "target_ref": target_ref,
        "target_commit": target,
        "source_commits_missing_from_local_checkout": absent,
        "warning": (
            "Ancestry only proves that commits are in the history; neither it "
            "nor an exact file blob proves runtime behavior or physical FAT. "
            "Missing checkout commits have no preservation conclusion."
        ),
        "records": records,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".", help="Checkout root")
    parser.add_argument("--verify-only", action="store_true", help="Check pinned source manifests without git")
    parser.add_argument("--scan-git", action="store_true", help="Inspect local commit ancestry and per-file blobs")
    parser.add_argument("--target", default="HEAD", help="Target ref (default HEAD)")
    parser.add_argument("--report", help="Write machine-readable audit results to JSON path")
    parser.add_argument(
        "--require-complete-history", action="store_true",
        help="Fail if any source PR commit has not been fetched into local git",
    )
    args = parser.parse_args()
    if args.verify_only and args.scan_git:
        parser.error("Choose --verify-only or --scan-git, not both")
    root = Path(args.repo).resolve()
    try:
        manifest, audits, independent = load_and_validate(root)
        print(
            f"PRESERVATION SNAPSHOT OK: {len(manifest['pull_requests'])} PRs, "
            f"{len(manifest['issues'])} issues, {len(audits)} side PR blob audits, "
            f"{len(independent)} independent PR source inventories"
        )
        if not args.scan_git:
            print("Ancestry and functional equivalence NOT assessed (manifest validation only).")
            return 0
        result = audit_git(root, manifest, audits, independent, args.target)
        missing = result["source_commits_missing_from_local_checkout"]
        divergent = [
            p["number"] for p in result["records"]
            if p["classification"] == "CANONICAL_CHAIN"
            and p["ancestry"] == "COMMIT_NOT_ANCESTOR_REVIEW"
        ]
        review_files = [
            (p["number"], f["path"], f["status"]) for p in result["records"]
            for f in p.get("side_changed_paths", [])
            if f["status"] in ("MISSING_FROM_TIP_REVIEW", "DIFFERENT_BLOB_SEMANTIC_REVIEW")
        ]
        print(f"TARGET {result['target_commit']}")
        print(f"Unavailable PR head objects: {len(missing)} (not proof of work loss)")
        print(f"Canonical chain heads requiring ancestry review: {divergent}")
        print(f"Side file paths requiring semantic review: {len(review_files)}")
        for number, path, state in review_files:
            if state == "MISSING_FROM_TIP_REVIEW":
                print(f"  SOURCE #{number}: {path} - {state}")
        if args.report:
            output = Path(args.report)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            print(f"REPORT {output}")
        if args.require_complete_history and missing:
            return 2
        return 0
    except (KeyError, OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"PRESERVATION AUDIT FAILED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
