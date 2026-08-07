#!/usr/bin/env python3
"""Run bounded, read-only IEC 61850 MMS discovery cycles and record evidence."""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import time
from typing import Any


def find_binary(root: pathlib.Path, override: str | None) -> pathlib.Path:
    if override:
        path = pathlib.Path(override)
        if not path.is_file():
            raise FileNotFoundError(f"Discovery binary not found: {path}")
        return path
    names = ["ariec61850_live_discover.exe", "ariec61850_live_discover"]
    candidates = []
    for name in names:
        candidates.extend([
            root / "build" / "Release" / name,
            root / "build" / name,
        ])
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "Build ariec61850_live_discover first; searched build/Release and build."
    )


def run_cycle(binary: pathlib.Path, args: argparse.Namespace) -> tuple[dict[str, Any], int, str]:
    command = [
        str(binary), args.host, str(args.port), "--model-json",
        "--timeout-ms", str(args.timeout_ms),
    ]
    if args.ied_name:
        command += ["--ied-name", args.ied_name]
    if args.no_types:
        command.append("--no-types")
    if args.no_datasets:
        command.append("--no-datasets")
    if args.no_rcb:
        command.append("--no-rcb")
    started = time.monotonic()
    process = subprocess.run(command, capture_output=True, text=True, check=False)
    elapsed_ms = int((time.monotonic() - started) * 1000)
    if process.returncode not in (0, 1):
        message = process.stderr.strip() or process.stdout.strip() or "discovery failed"
        raise RuntimeError(message)
    try:
        document = json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Discovery output is not valid JSON: {exc}") from exc
    if document.get("schemaVersion") != "live-ied-model-v1":
        raise RuntimeError("Discovery did not emit live-ied-model-v1 JSON.")
    return document, elapsed_ms, process.stderr.strip()


def parity(root: pathlib.Path, expected: pathlib.Path, observed: pathlib.Path, output: pathlib.Path) -> int:
    script = root / "scripts" / "compare-live-model-parity.py"
    process = subprocess.run(
        [sys.executable, str(script), str(expected), str(observed), "--output", str(output)],
        check=False,
    )
    return process.returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", nargs="?", type=int, default=102)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--interval-ms", type=int, default=1000)
    parser.add_argument("--timeout-ms", type=int, default=5000)
    parser.add_argument("--ied-name", default="")
    parser.add_argument("--binary")
    parser.add_argument("--expected-csharp-model")
    parser.add_argument("--allow-warnings", action="store_true")
    parser.add_argument("--no-types", action="store_true")
    parser.add_argument("--no-datasets", action="store_true")
    parser.add_argument("--no-rcb", action="store_true")
    args = parser.parse_args()

    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    if not 1 <= args.cycles <= 10000:
        parser.error("cycles must be 1..10000")
    if args.interval_ms < 0 or args.timeout_ms <= 0:
        parser.error("interval must be non-negative and timeout positive")

    root = pathlib.Path(__file__).resolve().parents[1]
    output_dir = pathlib.Path(args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    binary = find_binary(root, args.binary)

    cycles: list[dict[str, Any]] = []
    fingerprints: list[str] = []
    parity_codes: list[int] = []
    for index in range(1, args.cycles + 1):
        evidence: dict[str, Any] = {"index": index, "success": False}
        try:
            model, elapsed_ms, stderr = run_cycle(binary, args)
            model_path = output_dir / f"cycle-{index:03d}-live-model.json"
            model_path.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
            coverage = model.get("coverage", {})
            warnings = model.get("warnings", [])
            fingerprint = str(model.get("fingerprint", ""))
            fingerprints.append(fingerprint)
            evidence.update({
                "success": bool(coverage.get("logicalDeviceCount", 0))
                           and bool(coverage.get("dataAttributeCount", 0)),
                "elapsedMs": elapsed_ms,
                "fingerprint": fingerprint,
                "warningCount": len(warnings),
                "coverage": coverage,
                "stderr": stderr,
                "modelFile": model_path.name,
            })
            if args.expected_csharp_model:
                parity_path = output_dir / f"cycle-{index:03d}-csharp-cpp-parity.json"
                code = parity(
                    root,
                    pathlib.Path(args.expected_csharp_model).resolve(),
                    model_path,
                    parity_path,
                )
                parity_codes.append(code)
                evidence["parityExitCode"] = code
                evidence["parityFile"] = parity_path.name
        except Exception as exc:
            evidence["error"] = str(exc)
        cycles.append(evidence)
        print(
            f"Cycle {index}/{args.cycles}: success={evidence['success']} "
            f"fingerprint={evidence.get('fingerprint', '')} "
            f"warnings={evidence.get('warningCount', 0)}"
        )
        if index != args.cycles and args.interval_ms:
            time.sleep(args.interval_ms / 1000.0)

    stable = bool(fingerprints) and len(set(fingerprints)) == 1 and len(fingerprints) == args.cycles
    all_success = all(bool(cycle.get("success")) for cycle in cycles)
    warnings_ok = args.allow_warnings or all(cycle.get("warningCount", 0) == 0 for cycle in cycles)
    parity_ok = not args.expected_csharp_model or (
        len(parity_codes) == args.cycles and all(code == 0 for code in parity_codes)
    )
    accepted = all_success and stable and warnings_ok and parity_ok
    summary = {
        "schemaVersion": "ariec61850-live-interop-v1",
        "endpoint": f"{args.host}:{args.port}",
        "binary": os.fspath(binary),
        "cycleCount": args.cycles,
        "stableFingerprint": stable,
        "warningsAccepted": warnings_ok,
        "parityAccepted": parity_ok,
        "accepted": accepted,
        "cycles": cycles,
    }
    (output_dir / "interop-summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Read-only interoperability acceptance: {'PASS' if accepted else 'FAIL'}")
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
