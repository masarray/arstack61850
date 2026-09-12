#!/usr/bin/env python3
"""Deterministic large-SCL import benchmark for the desktop simulator.

Generates compact SCL sources whose reusable type templates expand to roughly
5k/20k/50k data attributes, then drives the same bounded async import path used
by the FileDialog.  The harness records wall time and Linux peak RSS and applies
intentionally generous regression budgets: the goal is to catch catastrophic
O(N^2), runaway-memory, deadlock, and crash regressions, not micro-benchmark CI.
"""

from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time

DO_PER_LN = 25
ATTRIBUTES_PER_DO = 3
ATTRIBUTES_PER_LN = DO_PER_LN * ATTRIBUTES_PER_DO


def build_scl(target_points: int) -> tuple[str, int]:
    ln_count = max(1, math.ceil(target_points / ATTRIBUTES_PER_LN))
    generated_points = ln_count * ATTRIBUTES_PER_LN

    logical_nodes = "\n".join(
        f'          <LN lnClass="GGIO" inst="{index}" lnType="GGIOBigType" />'
        for index in range(1, ln_count + 1)
    )
    data_objects = "\n".join(
        f'      <DO name="Ind{index}" type="SPCType" />'
        for index in range(1, DO_PER_LN + 1)
    )

    xml = f'''<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="ARSTACK_PERF_{target_points}" version="1" revision="A" />
  <IED name="PERF01" manufacturer="ARStack" type="Synthetic" configVersion="1">
    <AccessPoint name="P1">
      <Server>
        <LDevice inst="LD0">
          <LN0 lnClass="LLN0" lnType="LLN0Type" />
{logical_nodes}
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0Type" lnClass="LLN0" />
    <LNodeType id="GGIOBigType" lnClass="GGIO">
{data_objects}
    </LNodeType>
    <DOType id="SPCType" cdc="SPS">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="q" bType="Quality" fc="ST" />
      <DA name="t" bType="Timestamp" fc="ST" />
    </DOType>
  </DataTypeTemplates>
</SCL>
'''
    return xml, generated_points


def read_rss_kib(pid: int) -> int:
    try:
        for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError, PermissionError, ValueError):
        pass
    return 0


def run_case(app: Path, target: int, timeout_s: float) -> tuple[float, float, str]:
    xml, generated_points = build_scl(target)
    with tempfile.TemporaryDirectory(prefix="arstack-scl-perf-") as temporary:
        scl_path = Path(temporary) / f"synthetic-{target}.scd"
        scl_path.write_text(xml, encoding="utf-8")

        env = os.environ.copy()
        env["QT_QPA_PLATFORM"] = "offscreen"
        started = time.perf_counter()
        process = subprocess.Popen(
            [str(app), "--qa-async-import", str(scl_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env,
        )
        peak_rss_kib = 0
        output = ""
        try:
            while process.poll() is None:
                peak_rss_kib = max(peak_rss_kib, read_rss_kib(process.pid))
                if time.perf_counter() - started > timeout_s:
                    process.kill()
                    raise RuntimeError(
                        f"{target} point import exceeded hard timeout {timeout_s:.0f}s"
                    )
                time.sleep(0.02)
            stdout, _ = process.communicate(timeout=2)
            output = stdout or ""
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=2)

        elapsed_s = time.perf_counter() - started
        peak_rss_mib = peak_rss_kib / 1024.0
        if process.returncode != 0:
            raise RuntimeError(
                f"{target} point import exited {process.returncode}; output={output[-4000:]}"
            )
        if "ASYNC_IMPORT_OK" not in output:
            raise RuntimeError(
                f"{target} point import never reported async completion; output={output[-4000:]}"
            )

        summary = (
            f"SCL_PERF target={target} generated={generated_points} "
            f"elapsed_ms={elapsed_s * 1000:.0f} peak_rss_mib={peak_rss_mib:.1f}"
        )
        print(summary, flush=True)
        return elapsed_s, peak_rss_mib, summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument(
        "--targets",
        default="5000,20000,50000",
        help="Comma-separated expanded data-attribute targets.",
    )
    args = parser.parse_args()

    app = args.app.resolve()
    if not app.is_file():
        raise SystemExit(f"simulator executable not found: {app}")

    targets = [int(value) for value in args.targets.split(",") if value.strip()]
    budgets = {
        5_000: (15.0, 512.0),
        20_000: (30.0, 768.0),
        50_000: (55.0, 1024.0),
    }

    failures: list[str] = []
    for target in targets:
        time_budget, rss_budget = budgets.get(target, (60.0, 1024.0))
        elapsed, peak_rss, _ = run_case(app, target, timeout_s=max(65.0, time_budget + 10.0))
        if elapsed > time_budget:
            failures.append(
                f"{target}: {elapsed:.2f}s > {time_budget:.2f}s wall-time budget"
            )
        if peak_rss > rss_budget:
            failures.append(
                f"{target}: {peak_rss:.1f} MiB > {rss_budget:.1f} MiB RSS budget"
            )

    if failures:
        print("SCL_PERF_REGRESSION", flush=True)
        for failure in failures:
            print(f" - {failure}", flush=True)
        return 1

    print("SCL_PERF_OK", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
