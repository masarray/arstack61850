#!/usr/bin/env python3
"""Orchestrate bounded Phase 4C physical read-only acceptance evidence."""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import time
from typing import Any


CONTENTION_SUMMARY_RE = re.compile(
    r"^Read-only RCB contention probe: endpoint=(?P<endpoint>.*?), "
    r"associationProfile=(?P<association_profile>.*?), "
    r"associationAttempts=(?P<association_attempts>\d+), "
    r"RCB=(?P<rcb>.*?), probes=(?P<probes>\d+), "
    r"busy=(?P<busy>true|false), flapping=(?P<flapping>true|false), "
    r"contended=(?P<contended>true|false), decision=(?P<decision>.*?), "
    r"cooldownSec=(?P<cooldown>\d+)\.$"
)


def find_binary(root: pathlib.Path, name: str, override: str | None) -> pathlib.Path:
    if override:
        path = pathlib.Path(override).resolve()
        if not path.is_file():
            raise FileNotFoundError(f"Binary not found: {path}")
        return path

    names = [f"{name}.exe", name]
    candidates: list[pathlib.Path] = []
    for executable in names:
        candidates.extend([
            root / "build" / "Release" / executable,
            root / "build" / executable,
        ])
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        f"Build {name} first; searched build/Release and build."
    )


def run_process(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
    )


def parse_contention_summary(stdout: str) -> dict[str, Any]:
    first_line = stdout.splitlines()[0].strip() if stdout.splitlines() else ""
    match = CONTENTION_SUMMARY_RE.match(first_line)
    if not match:
        raise RuntimeError("Contention probe output did not match the expected summary format.")
    fields = match.groupdict()
    return {
        "endpoint": fields["endpoint"],
        "associationProfile": fields["association_profile"],
        "associationAttempts": int(fields["association_attempts"]),
        "rcb": fields["rcb"],
        "probeCount": int(fields["probes"]),
        "busy": fields["busy"] == "true",
        "flapping": fields["flapping"] == "true",
        "contended": fields["contended"] == "true",
        "decision": fields["decision"],
        "cooldownSeconds": int(fields["cooldown"]),
    }


def build_discovery_command(
    root: pathlib.Path,
    args: argparse.Namespace,
    output_dir: pathlib.Path,
) -> list[str]:
    command = [
        sys.executable,
        str(root / "scripts" / "run-live-readonly-interop.py"),
        args.host,
        str(args.port),
        "--output",
        str(output_dir),
        "--cycles",
        str(args.cycles),
        "--interval-ms",
        str(args.interval_ms),
        "--timeout-ms",
        str(args.timeout_ms),
    ]
    if args.discovery_binary:
        command += ["--binary", str(pathlib.Path(args.discovery_binary).resolve())]
    if args.ied_name:
        command += ["--ied-name", args.ied_name]
    if args.expected_csharp_model:
        command += [
            "--expected-csharp-model",
            str(pathlib.Path(args.expected_csharp_model).resolve()),
        ]
    if args.parity_types:
        command.append("--parity-types")
    if args.parity_runtime:
        command.append("--parity-runtime")
    if args.allow_warnings or args.fast_readonly:
        command.append("--allow-warnings")
    if args.fast_readonly or args.no_types:
        command.append("--no-types")
    elif args.max_types is not None:
        command += ["--max-types", str(args.max_types)]
    if args.fast_readonly or args.no_datasets:
        command.append("--no-datasets")
    if args.no_rcb:
        command.append("--no-rcb")
    elif args.max_rcb is not None:
        command += ["--max-rcb", str(args.max_rcb)]
    elif args.fast_readonly:
        command += ["--max-rcb", "10"]
    if args.require_rcb_complete:
        command.append("--require-rcb-complete")
    if args.control_block_values:
        command.append("--control-block-values")
        if args.max_control_blocks is not None:
            command += ["--max-control-blocks", str(args.max_control_blocks)]
        if args.max_control_block_attributes is not None:
            command += [
                "--max-control-block-attributes",
                str(args.max_control_block_attributes),
            ]
    if args.require_control_block_complete:
        command.append("--require-control-block-complete")
    return command


def run_contention_cycle(
    binary: pathlib.Path,
    args: argparse.Namespace,
    cycle: int,
    output_dir: pathlib.Path,
) -> dict[str, Any]:
    command = [
        str(binary),
        args.host,
        str(args.port),
        "--probe-count",
        str(args.contention_probe_count),
        "--probe-delay-ms",
        str(args.contention_probe_delay_ms),
        "--cooldown-sec",
        str(args.contention_cooldown_sec),
        "--timeout-ms",
        str(args.timeout_ms),
    ]
    if args.contention_rcb:
        command += ["--rcb", args.contention_rcb]

    started = time.monotonic()
    process = run_process(command)
    elapsed_ms = int((time.monotonic() - started) * 1000)
    raw_path = output_dir / f"contention-{cycle:03d}.txt"
    raw_path.write_text(
        process.stdout + (("\n" + process.stderr) if process.stderr else ""),
        encoding="utf-8",
    )

    evidence: dict[str, Any] = {
        "index": cycle,
        "success": process.returncode == 0,
        "elapsedMs": elapsed_ms,
        "exitCode": process.returncode,
        "rawFile": raw_path.name,
    }
    if process.returncode != 0:
        evidence["error"] = process.stderr.strip() or process.stdout.strip()
        return evidence

    try:
        evidence.update(parse_contention_summary(process.stdout))
    except Exception as exc:
        evidence["success"] = False
        evidence["error"] = str(exc)
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run repeated read-only MMS discovery/reassociation plus bounded RCB "
            "contention probes and emit Phase 4C physical acceptance evidence."
        )
    )
    parser.add_argument("host")
    parser.add_argument("port", nargs="?", type=int, default=102)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--interval-ms", type=int, default=1000)
    parser.add_argument("--timeout-ms", type=int, default=30000)
    parser.add_argument("--ied-name", default="")
    parser.add_argument("--discovery-binary")
    parser.add_argument("--expected-csharp-model")
    parser.add_argument("--parity-types", action="store_true")
    parser.add_argument("--parity-runtime", action="store_true")
    parser.add_argument("--allow-warnings", action="store_true")
    parser.add_argument(
        "--fast-readonly",
        action="store_true",
        help="Use --no-types --no-datasets --max-rcb 10 and allow expected warnings.",
    )
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
    parser.add_argument("--contention-binary")
    parser.add_argument("--contention-cycles", type=int, default=3)
    parser.add_argument("--contention-rcb", default="")
    parser.add_argument("--contention-probe-count", type=int, default=3)
    parser.add_argument("--contention-probe-delay-ms", type=int, default=1000)
    parser.add_argument("--contention-cooldown-sec", type=int, default=60)
    parser.add_argument(
        "--allow-contended",
        action="store_true",
        help="Record contention but do not fail acceptance when an RCB is busy/flapping.",
    )
    args = parser.parse_args()

    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    if not 1 <= args.cycles <= 10000:
        parser.error("--cycles must be 1..10000")
    if not 1 <= args.contention_cycles <= 10000:
        parser.error("--contention-cycles must be 1..10000")
    if args.interval_ms < 0 or args.timeout_ms <= 0:
        parser.error("interval must be non-negative and timeout positive")
    if args.contention_probe_count <= 0:
        parser.error("--contention-probe-count must be positive")
    if args.contention_probe_delay_ms < 0 or args.contention_cooldown_sec < 0:
        parser.error("contention delay/cooldown must be non-negative")
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
    if args.require_rcb_complete and (args.no_rcb or args.fast_readonly):
        parser.error(
            "--require-rcb-complete cannot be combined with --no-rcb/--fast-readonly"
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
            "--parity-types/--parity-runtime require --expected-csharp-model"
        )

    root = pathlib.Path(__file__).resolve().parents[1]
    output_root = pathlib.Path(args.output).resolve()
    discovery_dir = output_root / "discovery"
    contention_dir = output_root / "contention"
    discovery_dir.mkdir(parents=True, exist_ok=True)
    contention_dir.mkdir(parents=True, exist_ok=True)

    contention_binary = find_binary(
        root,
        "ariec61850_rcb_contention_probe",
        args.contention_binary,
    )

    discovery_command = build_discovery_command(root, args, discovery_dir)
    discovery_process = run_process(discovery_command)
    if discovery_process.stdout:
        print(discovery_process.stdout, end="")
    if discovery_process.stderr:
        print(discovery_process.stderr, end="", file=sys.stderr)

    discovery_summary_path = discovery_dir / "interop-summary.json"
    discovery_summary: dict[str, Any] = {}
    if discovery_summary_path.is_file():
        discovery_summary = json.loads(
            discovery_summary_path.read_text(encoding="utf-8")
        )

    contention_cycles: list[dict[str, Any]] = []
    for index in range(1, args.contention_cycles + 1):
        evidence = run_contention_cycle(
            contention_binary,
            args,
            index,
            contention_dir,
        )
        contention_cycles.append(evidence)
        print(
            f"Contention {index}/{args.contention_cycles}: "
            f"success={evidence.get('success', False)} "
            f"profile={evidence.get('associationProfile', '')} "
            f"attempts={evidence.get('associationAttempts', 0)} "
            f"contended={evidence.get('contended', False)} "
            f"decision={evidence.get('decision', '')}"
        )
        if index != args.contention_cycles and args.interval_ms:
            time.sleep(args.interval_ms / 1000.0)

    discovery_cycles = discovery_summary.get("cycles", [])
    successful_discovery_associations = sum(
        1 for cycle in discovery_cycles if cycle.get("success")
    )
    successful_contention_associations = sum(
        1 for cycle in contention_cycles if cycle.get("success")
    )
    contention_stable = all(
        cycle.get("success")
        and not cycle.get("contended", True)
        and cycle.get("decision") == "StableProceed"
        for cycle in contention_cycles
    )
    contention_accepted = all(
        cycle.get("success") for cycle in contention_cycles
    ) and (args.allow_contended or contention_stable)
    discovery_accepted = (
        discovery_process.returncode == 0
        and bool(discovery_summary.get("accepted"))
    )
    control_block_accepted = bool(
        discovery_summary.get("controlBlockCompletenessAccepted", True)
    )
    fresh_association_count = (
        successful_discovery_associations + successful_contention_associations
    )
    reassociation_accepted = (
        fresh_association_count == args.cycles + args.contention_cycles
    )
    accepted = discovery_accepted and contention_accepted and reassociation_accepted

    summary = {
        "schemaVersion": "ariec61850-phase4c-physical-acceptance-v2",
        "endpoint": f"{args.host}:{args.port}",
        "readOnly": True,
        "discovery": {
            "accepted": discovery_accepted,
            "exitCode": discovery_process.returncode,
            "cycleCount": args.cycles,
            "successfulFreshAssociations": successful_discovery_associations,
            "summaryFile": str(discovery_summary_path.relative_to(output_root)),
            "controlBlockValuesRequested": args.control_block_values,
            "controlBlockCompletenessRequired": args.require_control_block_complete,
            "controlBlockCompletenessAccepted": control_block_accepted,
        },
        "contention": {
            "accepted": contention_accepted,
            "stable": contention_stable,
            "allowContended": args.allow_contended,
            "cycleCount": args.contention_cycles,
            "successfulFreshAssociations": successful_contention_associations,
            "probeCountPerCycle": args.contention_probe_count,
            "cycles": contention_cycles,
        },
        "reassociationEvidence": {
            "freshProcessPerDiscoveryCycle": True,
            "freshProcessPerContentionCycle": True,
            "successfulFreshAssociationCount": fresh_association_count,
            "expectedFreshAssociationCount": args.cycles + args.contention_cycles,
            "accepted": reassociation_accepted,
        },
        "accepted": accepted,
    }
    summary_path = output_root / "phase4c-physical-acceptance-summary.json"
    summary_path.write_text(
        json.dumps(summary, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        "Phase 4C physical read-only acceptance: "
        + ("PASS" if accepted else "FAIL")
        + f" (discovery={discovery_accepted}, contention={contention_accepted}, "
        f"controlBlocks={control_block_accepted}, "
        f"freshAssociations={fresh_association_count}/"
        f"{args.cycles + args.contention_cycles})"
    )
    print(f"Summary: {summary_path}")
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
