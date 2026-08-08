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


CONTROL_BLOCK_PROPERTIES = (
    "gooseControlBlocks",
    "sampledValueControlBlocks",
    "settingGroupControls",
    "logControls",
)


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


def run_cycle(
    binary: pathlib.Path,
    args: argparse.Namespace,
) -> tuple[dict[str, Any], int, str]:
    command = [
        str(binary),
        args.host,
        str(args.port),
        "--model-json",
        "--timeout-ms",
        str(args.timeout_ms),
    ]
    if args.ied_name:
        command += ["--ied-name", args.ied_name]
    if args.no_types:
        command.append("--no-types")
    elif args.max_types is not None:
        command += ["--max-types", str(args.max_types)]
    if args.no_datasets:
        command.append("--no-datasets")
    if args.no_rcb:
        command.append("--no-rcb")
    elif args.max_rcb is not None:
        command += ["--max-rcb", str(args.max_rcb)]
    if args.control_block_values:
        command.append("--control-block-values")
        if args.max_control_blocks is not None:
            command += ["--max-control-blocks", str(args.max_control_blocks)]
        if args.max_control_block_attributes is not None:
            command += [
                "--max-control-block-attributes",
                str(args.max_control_block_attributes),
            ]

    started = time.monotonic()
    process = subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
    )
    elapsed_ms = int((time.monotonic() - started) * 1000)
    if process.returncode not in (0, 1):
        message = (
            process.stderr.strip()
            or process.stdout.strip()
            or "discovery failed"
        )
        raise RuntimeError(message)
    try:
        document = json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"Discovery output is not valid JSON: {exc}"
        ) from exc
    if document.get("schemaVersion") != "live-ied-model-v1":
        raise RuntimeError(
            "Discovery did not emit live-ied-model-v1 JSON."
        )
    return document, elapsed_ms, process.stderr.strip()


def parity(
    root: pathlib.Path,
    expected: pathlib.Path,
    observed: pathlib.Path,
    output: pathlib.Path,
    *,
    compare_types: bool,
    compare_runtime: bool,
) -> int:
    script = root / "tools" / "compare_live_model_json.py"
    command = [
        sys.executable,
        str(script),
        str(expected),
        str(observed),
        "--json",
    ]
    if compare_types:
        command.append("--types")
    if compare_runtime:
        command.append("--runtime")
    process = subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
    )
    if process.stdout:
        output.write_text(process.stdout, encoding="utf-8")
    if process.stderr:
        print(process.stderr, end="", file=sys.stderr)
    return process.returncode


def rcb_read_complete(coverage: dict[str, Any]) -> bool:
    total = int(coverage.get("reportControlCount", 0) or 0)
    not_read = int(
        coverage.get("reportControlBindingNotReadCount", 0) or 0
    )
    read_failed = int(
        coverage.get("reportControlBindingReadFailedCount", 0) or 0
    )
    return total > 0 and not_read == 0 and read_failed == 0


def control_block_read_summary(model: dict[str, Any]) -> dict[str, Any]:
    candidate_count = 0
    complete = 0
    partial = 0
    failed = 0
    not_read = 0

    for property_name in CONTROL_BLOCK_PROPERTIES:
        controls = model.get(property_name, [])
        if not isinstance(controls, list):
            continue
        for control in controls:
            if not isinstance(control, dict):
                continue
            candidate_count += 1
            status = str(control.get("discoveryStatus", ""))
            if status == "ValueReadComplete":
                complete += 1
            elif status == "ValueReadPartial":
                partial += 1
            elif status == "ValueReadFailed":
                failed += 1
            else:
                not_read += 1

    attempted = complete + partial + failed
    complete_read = (
        candidate_count > 0
        and attempted == candidate_count
        and complete == candidate_count
        and partial == 0
        and failed == 0
        and not_read == 0
    )
    return {
        "candidateCount": candidate_count,
        "attempted": attempted,
        "complete": complete,
        "partial": partial,
        "failed": failed,
        "notRead": not_read,
        "completeRead": complete_read,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", nargs="?", type=int, default=102)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--interval-ms", type=int, default=1000)
    parser.add_argument("--timeout-ms", type=int, default=120000)
    parser.add_argument("--ied-name", default="")
    parser.add_argument("--binary")
    parser.add_argument("--expected-csharp-model")
    parser.add_argument("--parity-types", action="store_true")
    parser.add_argument("--parity-runtime", action="store_true")
    parser.add_argument("--allow-warnings", action="store_true")
    parser.add_argument("--no-types", action="store_true")
    parser.add_argument("--max-types", type=int)
    parser.add_argument("--no-datasets", action="store_true")
    parser.add_argument("--no-rcb", action="store_true")
    parser.add_argument("--max-rcb", type=int)
    parser.add_argument("--require-rcb-complete", action="store_true")
    parser.add_argument("--control-block-values", action="store_true")
    parser.add_argument("--max-control-blocks", type=int)
    parser.add_argument("--max-control-block-attributes", type=int)
    parser.add_argument("--require-control-block-complete", action="store_true")
    args = parser.parse_args()

    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    if not 1 <= args.cycles <= 10000:
        parser.error("cycles must be 1..10000")
    if args.interval_ms < 0 or args.timeout_ms <= 0:
        parser.error(
            "interval must be non-negative and timeout positive"
        )
    if args.max_types is not None and args.max_types <= 0:
        parser.error("--max-types must be positive")
    if args.max_rcb is not None and args.max_rcb <= 0:
        parser.error("--max-rcb must be positive")
    if args.max_control_blocks is not None and args.max_control_blocks <= 0:
        parser.error("--max-control-blocks must be positive")
    if (
        args.max_control_block_attributes is not None
        and args.max_control_block_attributes <= 0
    ):
        parser.error("--max-control-block-attributes must be positive")
    if args.no_types and args.max_types is not None:
        parser.error("--no-types cannot be combined with --max-types")
    if args.no_rcb and args.max_rcb is not None:
        parser.error("--no-rcb cannot be combined with --max-rcb")
    if args.require_rcb_complete and args.no_rcb:
        parser.error(
            "--require-rcb-complete cannot be combined with --no-rcb"
        )
    if (
        args.max_control_blocks is not None
        or args.max_control_block_attributes is not None
        or args.require_control_block_complete
    ) and not args.control_block_values:
        parser.error(
            "--max-control-blocks/--max-control-block-attributes/"
            "--require-control-block-complete require --control-block-values"
        )
    if (args.parity_types or args.parity_runtime) and not args.expected_csharp_model:
        parser.error(
            "--parity-types/--parity-runtime require "
            "--expected-csharp-model"
        )

    root = pathlib.Path(__file__).resolve().parents[1]
    output_dir = pathlib.Path(args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    binary = find_binary(root, args.binary)

    cycles: list[dict[str, Any]] = []
    structural_fingerprints: list[str] = []
    runtime_fingerprints: list[str] = []
    legacy_fingerprints: list[str] = []
    parity_codes: list[int] = []
    rcb_complete_flags: list[bool] = []
    control_block_complete_flags: list[bool] = []

    for index in range(1, args.cycles + 1):
        evidence: dict[str, Any] = {"index": index, "success": False}
        try:
            model, elapsed_ms, stderr = run_cycle(binary, args)
            model_path = output_dir / (
                f"cycle-{index:03d}-live-model.json"
            )
            model_path.write_text(
                json.dumps(model, indent=2) + "\n",
                encoding="utf-8",
            )
            coverage = model.get("coverage", {})
            warnings = model.get("warnings", [])
            legacy_fingerprint = str(model.get("fingerprint", ""))
            structural_fingerprint = str(
                model.get(
                    "structuralFingerprint",
                    legacy_fingerprint,
                )
            )
            runtime_fingerprint = str(
                model.get("runtimeSnapshotFingerprint", "")
            )
            legacy_fingerprints.append(legacy_fingerprint)
            structural_fingerprints.append(
                structural_fingerprint
            )
            if runtime_fingerprint:
                runtime_fingerprints.append(
                    runtime_fingerprint
                )

            cycle_rcb_complete = rcb_read_complete(coverage)
            rcb_complete_flags.append(cycle_rcb_complete)
            control_block_summary = control_block_read_summary(model)
            if args.control_block_values:
                control_block_complete_flags.append(
                    bool(control_block_summary["completeRead"])
                )
            evidence.update({
                "success": bool(
                    coverage.get("logicalDeviceCount", 0)
                )
                and bool(coverage.get("dataAttributeCount", 0)),
                "elapsedMs": elapsed_ms,
                "fingerprint": structural_fingerprint,
                "structuralFingerprint": structural_fingerprint,
                "runtimeSnapshotFingerprint": runtime_fingerprint,
                "legacyCanonicalFingerprint": legacy_fingerprint,
                "warningCount": len(warnings),
                "coverage": coverage,
                "rcbReadComplete": cycle_rcb_complete,
                "controlBlockValues": control_block_summary,
                "controlBlockReadComplete": (
                    control_block_summary["completeRead"]
                    if args.control_block_values
                    else None
                ),
                "stderr": stderr,
                "modelFile": model_path.name,
            })

            if args.expected_csharp_model:
                parity_path = output_dir / (
                    f"cycle-{index:03d}-csharp-cpp-parity.json"
                )
                code = parity(
                    root,
                    pathlib.Path(
                        args.expected_csharp_model
                    ).resolve(),
                    model_path,
                    parity_path,
                    compare_types=args.parity_types,
                    compare_runtime=args.parity_runtime,
                )
                parity_codes.append(code)
                evidence["parityExitCode"] = code
                evidence["parityFile"] = parity_path.name
        except Exception as exc:
            evidence["error"] = str(exc)

        cycles.append(evidence)
        cb_text = (
            str(evidence.get("controlBlockReadComplete", False))
            if args.control_block_values
            else "n/a"
        )
        print(
            f"Cycle {index}/{args.cycles}: "
            f"success={evidence['success']} "
            f"structure={evidence.get('structuralFingerprint', '')} "
            f"runtime={evidence.get('runtimeSnapshotFingerprint', '')} "
            f"rcbComplete={evidence.get('rcbReadComplete', False)} "
            f"cbComplete={cb_text} "
            f"warnings={evidence.get('warningCount', 0)}"
        )
        if index != args.cycles and args.interval_ms:
            time.sleep(args.interval_ms / 1000.0)

    structural_stable = (
        bool(structural_fingerprints)
        and len(structural_fingerprints) == args.cycles
        and len(set(structural_fingerprints)) == 1
    )
    runtime_stable = (
        bool(runtime_fingerprints)
        and len(runtime_fingerprints) == args.cycles
        and len(set(runtime_fingerprints)) == 1
    )
    legacy_stable = (
        bool(legacy_fingerprints)
        and len(legacy_fingerprints) == args.cycles
        and len(set(legacy_fingerprints)) == 1
    )
    all_success = all(
        bool(cycle.get("success")) for cycle in cycles
    )
    warnings_ok = args.allow_warnings or all(
        cycle.get("warningCount", 0) == 0 for cycle in cycles
    )
    parity_ok = not args.expected_csharp_model or (
        len(parity_codes) == args.cycles
        and all(code == 0 for code in parity_codes)
    )
    rcb_complete_ok = (
        not args.require_rcb_complete
        or (
            len(rcb_complete_flags) == args.cycles
            and all(rcb_complete_flags)
        )
    )
    control_block_complete_ok = (
        not args.require_control_block_complete
        or (
            len(control_block_complete_flags) == args.cycles
            and all(control_block_complete_flags)
        )
    )
    accepted = (
        all_success
        and structural_stable
        and warnings_ok
        and parity_ok
        and rcb_complete_ok
        and control_block_complete_ok
    )

    summary = {
        "schemaVersion": "ariec61850-live-interop-v3",
        "endpoint": f"{args.host}:{args.port}",
        "binary": os.fspath(binary),
        "cycleCount": args.cycles,
        "stableFingerprint": structural_stable,
        "fingerprintKind": "structuralFingerprint",
        "stableStructuralFingerprint": structural_stable,
        "stableRuntimeSnapshotFingerprint": runtime_stable,
        "stableLegacyCanonicalFingerprint": legacy_stable,
        "warningsAccepted": warnings_ok,
        "parityAccepted": parity_ok,
        "parityTypesRequested": args.parity_types,
        "parityRuntimeRequested": args.parity_runtime,
        "rcbCompletenessRequired": args.require_rcb_complete,
        "rcbCompletenessAccepted": rcb_complete_ok,
        "controlBlockValuesRequested": args.control_block_values,
        "controlBlockCompletenessRequired": args.require_control_block_complete,
        "controlBlockCompletenessAccepted": control_block_complete_ok,
        "accepted": accepted,
        "cycles": cycles,
    }
    (output_dir / "interop-summary.json").write_text(
        json.dumps(summary, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        "Read-only interoperability acceptance: "
        + ("PASS" if accepted else "FAIL")
        + f" (structuralStable={structural_stable}, "
        f"runtimeStable={runtime_stable}, "
        f"rcbComplete={rcb_complete_ok}, "
        f"controlBlockComplete={control_block_complete_ok})"
    )
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
