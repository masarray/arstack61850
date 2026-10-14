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
    "arstack-studio-rc2-policy.yml",
    "arstack-studio-windows-lifecycle.yml",
    "ptp-core-ci.yml",
    "smv-injector-gui.yml",
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
                 "scl-assisted-connect-ci.yml",
                 "arstack-studio-rc2-policy.yml", "arstack-studio-windows-lifecycle.yml",
                 "ptp-core-ci.yml", "smv-injector-gui.yml"]:
        assert re.search(r"(?m)^    branches: \[main\]$", event_body(sources[name], "push")), name
    assert matching(paths(sources["ied-simulator-qt.yml"]), NORMAL_GUI)
    assert matching(paths(sources["scl-assisted-workbench-ci.yml"]), MODEL_CPP)
    assert not matching(paths(sources["ied-simulator-release-hardening.yml"]), NORMAL_GUI)
    assert matching(paths(sources["ied-simulator-release-hardening.yml"]), PACKAGING)
    # PTP / SMV firmware uses one source-branch key for PR runs; repetitive
    # feature pushes never schedule additional ESP-IDF builds.
    for name in ("esp32p4-smv-injector.yml", "esp32p4-ptp-lab.yml"):
        fw = (ROOT / name).read_text(encoding="utf-8")
        assert "github.event.pull_request.head.ref || github.ref_name" in fw, name
        assert "github.event.pull_request.head.repo.full_name || github.repository" in fw, name
        assert "github.ref != 'refs/heads/main'" in fw, name
    # WIP iteration must not spend Windows/ESP-IDF/3-compiler runners.
    # Ready-for-review, main integration and explicit workflow_dispatch remain.
    expensive = ("esp32p4-ptp-lab.yml", "esp32p4-smv-injector.yml", "ptp-core-ci.yml", "cpp-ci.yml", "embedded-ci.yml", "security-evidence.yml", "arstack-studio-qt.yml", "interoperability-server-ci.yml", "ied-simulator-release-hardening.yml", "ied-simulator-qt.yml", "arstack-studio-windows-lifecycle.yml", "brcb-hard-profile-ci.yml",)
    for name in expensive:
        source = (ROOT / name).read_text(encoding="utf-8")
        assert "ready_for_review" in event_body(source, "pull_request"), name
        job_part = source.split("\njobs:\n", 1)[1]
        job_names = re.findall(r"(?m)^  [a-zA-Z0-9_-]+:", job_part)
        draft_gates = re.findall(
            r"(?m)^    if: .*github.event.pull_request.draft.*$",
            job_part,
        )
        assert len(draft_gates) == len(job_names), (name, len(draft_gates), len(job_names))
    for name in ("esp32p4-smv-injector.yml", "esp32p4-ptp-lab.yml"):
        firmware = (ROOT / name).read_text(encoding="utf-8")
        push = event_body(firmware, "push")
        assert "branches: [main]" in push, f"{name}: feature push flood"
        assert "workflow_dispatch:" in firmware, f"{name}: missing manual candidate"
    release = (ROOT / "arstack-studio-release.yml").read_text(encoding="utf-8")
    assert not re.search(r"(?m)^  pull_request:", release), "installer on every PR"
    assert "workflow_dispatch:" in release and "tags:" in release
    print("CI_ROUTING_POLICY_PASS draft_runner_gates=12 firmware_feature_push=off release_pr_packaging=off manual_candidates=on")


if __name__ == "__main__":
    main()
