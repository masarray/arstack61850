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
    # Real routing guarantees: no feature-push firmware fanout, no
    # PR-triggered installer, no Draft PR runner minutes for heavy matrices.
    heavy_pr = (
        "esp32p4-ptp-lab.yml", "esp32p4-smv-injector.yml",
        "cpp-ci.yml", "embedded-ci.yml", "security-evidence.yml",
        "ptp-core-ci.yml", "arstack-studio-qt.yml",
        "arstack-studio-windows-lifecycle.yml", "ied-simulator-qt.yml",
        "ied-simulator-release-hardening.yml", "brcb-hard-profile-ci.yml",
        "control-hard-profile-ci.yml", "control-mms-profile-ci.yml",
        "control-session-profile-ci.yml", "control-termination-profile-ci.yml",
        "interoperability-server-ci.yml", "scl-assisted-connect-ci.yml",
        "scl-assisted-workbench-ci.yml", "smv-injector-gui.yml",
        "arstack-studio-rc2-policy.yml", "mms-r12-server-ci.yml",
        "control-interop-harness-ci.yml", "dynamic-rcb-trial-ci.yml",
    )
    gate = (
        "    if: ${{ github.event_name != 'pull_request' || "
        "!github.event.pull_request.draft }}"
    )
    for name in heavy_pr:
        source = (ROOT / name).read_text(encoding="utf-8")
        trigger = event_body(source, "pull_request")
        assert "ready_for_review" in trigger, f"{name}: no ready transition"
        assert "paths:" in trigger, f"{name}: unscoped PR"
        jobs = source.split("\njobs:\n", 1)[1]
        names = re.findall(r"(?m)^  [a-z0-9_-]+:\s*$", jobs)
        guarded = re.findall(
            r"(?m)^  [a-z0-9_-]+:\s*\n" + re.escape(gate) + r"\s*$",
            jobs,
        )
        assert names and len(guarded) == len(names), (
            name, len(guarded), len(names)
        )

    for name in ("esp32p4-smv-injector.yml", "esp32p4-ptp-lab.yml"):
        source = (ROOT / name).read_text(encoding="utf-8")
        push = event_body(source, "push")
        assert "branches: [main]" in push, f"{name}: feature push flood"
        assert "paths:" in push, f"{name}: unscoped main build"
        assert re.search(r"(?m)^  workflow_dispatch:\s*$", source), (
            name, "missing on-demand board artifact"
        )

    main_path_scoped = (
        "cpp-ci.yml", "security-evidence.yml", "embedded-ci.yml",
        "brcb-hard-profile-ci.yml", "control-hard-profile-ci.yml",
        "control-mms-profile-ci.yml", "control-session-profile-ci.yml",
        "control-termination-profile-ci.yml",
    )
    for name in main_path_scoped:
        source = (ROOT / name).read_text(encoding="utf-8")
        push = event_body(source, "push")
        assert "paths:" in push, f"{name}: full suite on every main push"

    release = (ROOT / "arstack-studio-release.yml").read_text(encoding="utf-8")
    assert not re.search(r"(?m)^  pull_request:", release), (
        "Studio installer packaging must be manually dispatched or tagged"
    )
    assert "workflow_dispatch:" in release and "tags:" in release
    interop = (ROOT / "interoperability-server-ci.yml").read_text(
        encoding="utf-8"
    )
    assert not matching(
        paths(interop), "include/ariec61850/time_sync/ptp_runtime.hpp"
    ), "PTP-only headers must not trigger MMS server CI"

    print(
        "CI_ROUTING_POLICY_PASS: "
        "draft_gate_workflows=23; feature_firmware_push=off; "
        "main_path_scoped=8; installer_pr=off; "
        "targeted_manual_firmware=on"
    )


if __name__ == "__main__":
    main()
