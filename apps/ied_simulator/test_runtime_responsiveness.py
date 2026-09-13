#!/usr/bin/env python3
"""Milestone-I runtime-scale and GUI responsiveness evidence."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from benchmark_large_scl import build_scl


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
            if f"prepared=1" not in output:
                raise RuntimeError(f"{target} point run did not prove the profile was prebuilt")
            print(f"RUNTIME_SCALE_CASE target={target} generated={generated} pass=true")

    multi_output = run_checked(
        [str(qa), "--responsiveness", str(multi_scl)],
        30.0,
        "IEDSIM_RESPONSIVENESS_PASS",
    )
    if "ieds=2 prepared=2" not in multi_output:
        raise RuntimeError("multi-IED responsiveness run did not prove both profiles were prebuilt")

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

    print("RUNTIME_SCALE_RESPONSIVENESS_PASS large=20000,50000 multi_ied=pass fleet_rollback=pass nonblocking_clear=pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
