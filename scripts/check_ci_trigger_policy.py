#!/usr/bin/env python3
"""Fail fast if unrelated IEC 61850 workflow matrices flood a Workbench-only PR."""
from pathlib import Path
import fnmatch
import re

ROOT = Path(__file__).resolve().parents[1] / ".github" / "workflows"
NAME = [
    "brcb-hard-profile-ci.yml",
    "control-interop-harness-ci.yml",
    "cpp-ci.yml",
    "dynamic-rcb-trial-ci.yml",
    "embedded-ci.yml",
    "security-evidence.yml",
    "ied-simulator-qt.yml",
    "ied-simulator-release-hardening.yml",
    "scl-assisted-workbench-ci.yml",
    "arstack-studio-qt.yml",
    "scl-assisted-connect-ci.yml",
]
NON_GUI = set(NAME[:6])
NORMAL_GUI = "apps/ied_simulator/qml/BrowserDataModelPane.qml"
MODEL_CPP = "apps/ied_simulator/src/MmsLiveTreeModel.cpp"
PACKAGING = "apps/ied_simulator/packaging/windows/ARStackIEC61850Workbench.nsi"


def event_body(source, event):
    match = re.search(r"(?m)^  " + re.escape(event) + r":\s*\n", source)
    if not match:
        raise AssertionError(f"missing event: {event}")
    rest = source[match.end():]
    end = re.search(r"(?m)^  [a-z_]+:\s*(?:\n|$)|^[a-z][\w-]*:", rest)
    return rest[:end.start()] if end else rest


def paths(source):
    body = event_body(source, "pull_request")
    assert re.search(r"(?m)^    paths:\s*$", body), "unscoped pull_request"
    return re.findall(r"(?m)^      - ['\"]([^'\"]+)['\"]\s*$", body)


def matching(patterns, path):
    return any(fnmatch.fnmatchcase(path, p) for p in patterns)


def main():
    sources = {name: (ROOT / name).read_text(encoding="utf-8") for name in NAME}
    for name, source in sources.items():
        pr_paths = paths(source)
        assert pr_paths, f"{name}: empty paths"
        assert f".github/workflows/{name}" in pr_paths, f"{name}: workflow self-check missing"
        assert re.search(r"(?m)^concurrency:", source), f"{name}: no concurrency"
        assert "github.event.pull_request.number || github.ref" in source, f"{name}: wrong PR group"
        assert "github.event_name == 'pull_request'" in source, f"{name}: stale runs not cancelled"
        assert not matching(pr_paths, NORMAL_GUI) if name in NON_GUI else True, f"{name}: GUI triggers unrelated job"
    for name in ["ied-simulator-qt.yml", "ied-simulator-release-hardening.yml",
                 "scl-assisted-workbench-ci.yml", "arstack-studio-qt.yml",
                 "scl-assisted-connect-ci.yml"]:
        assert re.search(r"(?m)^    branches: \[main\]$", event_body(sources[name], "push")), name
    assert matching(paths(sources["ied-simulator-qt.yml"]), NORMAL_GUI)
    assert matching(paths(sources["scl-assisted-workbench-ci.yml"]), MODEL_CPP)
    assert not matching(paths(sources["ied-simulator-release-hardening.yml"]), NORMAL_GUI)
    assert matching(paths(sources["ied-simulator-release-hardening.yml"]), PACKAGING)
    print("CI_ROUTING_POLICY_PASS non_gui_excluded=6 duplicate_push=0 pr_concurrency=11 release_is_scoped=true")


if __name__ == "__main__":
    main()
