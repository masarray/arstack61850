#!/usr/bin/env python3
"""Milestone-I runtime-scale and GUI responsiveness evidence."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from benchmark_large_scl import build_scl

RESPONSIVENESS_RE = re.compile(
    r"IEDSIM_RESPONSIVENESS_PASS\s+ieds=(?P<ieds>\d+)\s+prepared=(?P<prepared>\d+)"
)


def run_checked(command: list[str], timeout_s: float, marker: str) -> str:
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
        timeout=timeout_s,
        check=False,
    )
    output = completed.stdout or ""
    print(output, end="" if output.endswith("\n") else "\n")
    if completed.returncode != 0:
        raise RuntimeError(
            f"{' '.join(command)} exited {completed.returncode}; output={output[-5000:]}"
        )
    if marker not in output:
        raise RuntimeError(f"missing {marker}; output={output[-5000:]}")
    return output


def prepared_counts(output: str) -> tuple[int, int]:
    matches = list(RESPONSIVENESS_RE.finditer(output))
    if not matches:
        raise RuntimeError(f"missing responsiveness profile counts; output={output[-5000:]}")
    match = matches[-1]
    return int(match.group("ieds")), int(match.group("prepared"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qa", required=True, type=Path)
    parser.add_argument("--multi-scl", required=True, type=Path)
    parser.add_argument("--single-scl", required=True, type=Path)
    args = parser.parse_args()

    qa = args.qa.resolve()
    multi_scl = args.multi_scl.resolve()
    single_scl = args.single_scl.resolve()
    if not qa.is_file():
        raise RuntimeError(f"QA executable not found: {qa}")

    with tempfile.TemporaryDirectory(prefix="arstack-runtime-scale-") as temporary:
        root = Path(temporary)
        for target in (20_000, 50_000):
            xml, generated = build_scl(target)
            scl = root / f"runtime-{target}.scd"
            scl.write_text(xml, encoding="utf-8")
            output = run_checked(
                [str(qa), "--responsiveness", str(scl)],
                120.0,
                "IEDSIM_RESPONSIVENESS_PASS",
            )
            ieds, prepared = prepared_counts(output)
            if ieds != 1 or prepared != 1:
                raise RuntimeError(
                    f"{target} point run expected one fully prebuilt IED, got ieds={ieds} prepared={prepared}"
                )
            print(f"RUNTIME_SCALE_CASE target={target} generated={generated} pass=true")

    multi_output = run_checked(
        [str(qa), "--responsiveness", str(multi_scl)],
        30.0,
        "IEDSIM_RESPONSIVENESS_PASS",
    )
    ieds, prepared = prepared_counts(multi_output)
    if ieds < 2 or prepared != ieds:
        raise RuntimeError(
            f"multi-IED responsiveness run did not fully prebuild the fleet: ieds={ieds} prepared={prepared}"
        )

    run_checked(
        [str(qa), "--fleet-rollback", str(multi_scl)],
        30.0,
        "FLEET_PARTIAL_START_RECOVERY_PASS",
    )
    run_checked(
        [str(qa), "--clear-nonblocking", str(single_scl)],
        30.0,
        "NONBLOCKING_CLEAR_PASS",
    )

    print(
        "RUNTIME_SCALE_RESPONSIVENESS_PASS large=20000,50000 "
        f"multi_ied={ieds} fleet_rollback=pass nonblocking_clear=pass"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
