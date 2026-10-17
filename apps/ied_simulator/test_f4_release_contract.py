#!/usr/bin/env python3
"""Fail-closed regression for F4's staged/portable/installed source contract.

This is a cheap guard against build-workflow drift; the F4 Windows job still
must perform the actual NSIS install, ZIP extraction, and smoke tests.
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
F4 = ROOT / ".github/workflows/ied-real-device-f4.yml"
VERIFY = ROOT / "apps/ied_simulator/packaging/windows/verify-release.ps1"
NSI = ROOT / "apps/ied_simulator/packaging/windows/ARStackIEC61850Workbench.nsi"


def check(workflow: str, verifier: str, installer: str) -> None:
    failures: list[str] = []

    def require(haystack: str, needle: str, label: str) -> None:
        if needle not in haystack:
            failures.append(label)

    require(workflow, "pull_request:", "exact-head PR trigger")
    require(workflow, "branches: [main]", "post-merge main trigger")
    require(workflow, "workflow_dispatch:", "manual on-demand kit rebuild")
    require(workflow, "test_f4_release_contract.py", "static contract gate")
    require(workflow, "-PortablePath ./release-artifacts/ARStack-IEC61850-Workbench-portable-win64.zip",
            "real portable ZIP verified")
    require(workflow, "-ExpectedSourceSha ${{ github.sha }}", "exact GitHub SHA bound")
    require(workflow, "'BUILD_HEAD.txt'", "source SHA staged and copied into kit")
    require(workflow, "git rev-parse --verify HEAD", "git checkout SHA checked")
    require(workflow, "$sourceSha -cne $env:GITHUB_SHA", "fail closed on wrong checkout")
    require(workflow, "$stageHead -cne $env:GITHUB_SHA", "fail closed on stale kit SHA")
    require(workflow, "/DAPP_ICON=$icon", "mandatory NSIS icon supplied")
    require(workflow, "MAKENSIS_PATH", "NSIS preflight result reused")
    require(workflow, "F4_BUILD_MANIFEST.json", "kit manifest retained")
    require(workflow, "SHA256SUMS.txt", "kit hash manifest retained")
    require(workflow, "externalAcceptance = 'pending_external_client'",
            "physical/vendor acceptance never auto-passed")

    require(verifier, "[string]$PortablePath", "verifier ZIP argument")
    require(verifier, "[string]$ExpectedSourceSha", "verifier commit argument")
    require(verifier, "Assert-BuildHead $stage", "staged build-head checked")
    require(verifier, "Assert-BuildHead $portableDir", "actual ZIP build-head checked")
    require(verifier, "Assert-BuildHead $installDir", "installed build-head checked")
    require(verifier, "Portable ZIP smoke test", "actual ZIP binary smoke-launched")
    require(verifier, "Installed package smoke test", "NSIS-installed binary smoke-launched")
    require(installer, "!ifndef APP_ICON", "NSIS rejects omitted icon")

    if failures:
        raise RuntimeError("F4_RELEASE_CONTRACT_FAIL " + ", ".join(failures))


def main() -> None:
    f4 = F4.read_text(encoding="utf-8")
    verify = VERIFY.read_text(encoding="utf-8")
    nsi = NSI.read_text(encoding="utf-8")
    check(f4, verify, nsi)

    # Mutation controls prove that omissions in either source are rejected.
    for source, needle in (
        ("f4", "-ExpectedSourceSha ${{ github.sha }}"),
        ("f4", "/DAPP_ICON=$icon"),
        ("f4", "$stageHead -cne $env:GITHUB_SHA"),
        ("verify", "Assert-BuildHead $installDir"),
        ("verify", "Portable ZIP smoke test"),
        ("nsi", "!ifndef APP_ICON"),
    ):
        original = {"f4": f4, "verify": verify, "nsi": nsi}
        original[source] = original[source].replace(needle, "BROKEN_CONTRACT", 1)
        try:
            check(original["f4"], original["verify"], original["nsi"])
        except RuntimeError as exc:
            if "F4_RELEASE_CONTRACT_FAIL" not in str(exc):
                raise
        else:
            raise AssertionError("Contract regression failed to detect " + needle)
    print("F4_RELEASE_CONTRACT_PASS source=verified zip=verified installed=verified")


if __name__ == "__main__":
    main()
