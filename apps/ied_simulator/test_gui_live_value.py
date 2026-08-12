#!/usr/bin/env python3
"""Prove full SCL materialization and GUI-applied state are visible over MMS."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def creation_flags() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def resolve_read_probe(argument: str) -> str:
    path = Path(argument)
    if path.is_file():
        return str(path)
    if path.is_dir():
        names = {"ariec61850_mms_read_probe", "ariec61850_mms_read_probe.exe"}
        matches = sorted(
            candidate
            for candidate in path.rglob("ariec61850_mms_read_probe*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0])
    raise FileNotFoundError(f"MMS read probe not found under {path}")


def run_probe(read_probe: str, port: int, item: str, *, type_only: bool = False) -> subprocess.CompletedProcess[str]:
    command = [
        read_probe,
        "127.0.0.1",
        str(port),
        "--domain",
        "MU01LD0",
        "--item",
        item,
        "--timeout-ms",
        "3000",
    ]
    if type_only:
        command.append("--type")
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=6,
        check=False,
        creationflags=creation_flags(),
    )


def require_probe(
    probe: subprocess.CompletedProcess[str],
    expected: str,
    description: str,
) -> None:
    if probe.returncode != 0 or expected not in probe.stdout:
        raise RuntimeError(
            f"{description} failed: exit={probe.returncode} "
            f"stdout={probe.stdout} stderr={probe.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--read-probe", required=True)
    parser.add_argument("--scl", required=True)
    args = parser.parse_args()
    read_probe = resolve_read_probe(args.read_probe)

    port = free_port()
    environment = dict(os.environ)
    environment["QT_QPA_PLATFORM"] = "offscreen"
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as app_log:
        app = subprocess.Popen(
            [
                args.app,
                "--scl",
                args.scl,
                "--port",
                str(port),
                "--runtime",
                "--set-first-value",
                "42",
                "--exit-after-ms",
                "30000",
            ],
            stdout=app_log,
            stderr=subprocess.STDOUT,
            text=True,
            env=environment,
            creationflags=creation_flags(),
        )
        last_error = "server not ready"
        try:
            manifest_path = (
                Path(tempfile.gettempdir()) / f"arstack-ied-simulator-{app.pid}.model"
            )
            manifest_deadline = time.monotonic() + 8.0
            while time.monotonic() < manifest_deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                try:
                    manifest_text = manifest_path.read_text(encoding="utf-8")
                except (FileNotFoundError, PermissionError, UnicodeDecodeError):
                    manifest_text = ""
                gui_value_present = (
                    "TCTR1$MX$Amp$instMag$i\tINT16\tNumber\tinteger:16\t42"
                    in manifest_text
                )
                ordered_range_present = (
                    "TCTR1$MX$Amp$instMag$range\tINT8U\tNumber\tunsigned-integer:8\t0"
                    in manifest_text
                )
                # XCBR1.Health is deliberately absent from every DataSet. Its presence
                # proves DataTypeTemplates materialization, not FCDA-driven projection.
                structural_only_leaf_present = (
                    "XCBR1$ST$Health$stVal\tBOOLEAN\tBoolean\tboolean\tfalse"
                    in manifest_text
                )
                data_set_present = (
                    "DS\tMU01LD0\tLLN0$dsSV\tMU01LD0\tTCTR1$MX$Amp$instMag$i"
                    in manifest_text
                )
                rcb_present = (
                    "RCB\tMU01LD0\tLLN0$RP$urcb01\t0\tMU01_LD0_URCB01\t"
                    "MU01LD0\tLLN0$dsSV\t7\t20\t1000\t0" in manifest_text
                )
                no_ln_scaffold = "\nLN\t" not in "\n" + manifest_text
                order_preserved = (
                    "TCTR1$MX$Amp$instMag$i" in manifest_text
                    and "TCTR1$MX$Amp$instMag$range" in manifest_text
                    and manifest_text.index("TCTR1$MX$Amp$instMag$i")
                    < manifest_text.index("TCTR1$MX$Amp$instMag$range")
                )
                if (
                    manifest_text.startswith("ARSTACK_IED_MODEL\t4\t2\n")
                    and gui_value_present
                    and ordered_range_present
                    and structural_only_leaf_present
                    and data_set_present
                    and rcb_present
                    and no_ln_scaffold
                    and order_preserved
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError(
                    "GUI did not publish the complete v4 P0 model with exact types, "
                    "DataSet, RCB, and structural-only leaves"
                )

            deadline = time.monotonic() + 15.0
            while time.monotonic() < deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                time.sleep(0.25)
                edited_probe = run_probe(
                    read_probe, port, "TCTR1$MX$Amp$instMag$i"
                )
                if edited_probe.returncode != 0 or "value=42" not in edited_probe.stdout:
                    last_error = (
                        f"edited read exit={edited_probe.returncode} "
                        f"stdout={edited_probe.stdout} stderr={edited_probe.stderr}"
                    )
                    time.sleep(0.2)
                    continue

                # P0 exact TypeSpecification is proven on the wire with
                # GetVariableAccessAttributes, not inferred from manifest text.
                exact_type_probe = run_probe(
                    read_probe,
                    port,
                    "TCTR1$MX$Amp$instMag$i",
                    type_only=True,
                )
                require_probe(
                    exact_type_probe,
                    "kind=integer size=16",
                    "GVAA INT16 evidence",
                )
                exact_unsigned_probe = run_probe(
                    read_probe,
                    port,
                    "TCTR1$MX$Amp$instMag$range",
                    type_only=True,
                )
                require_probe(
                    exact_unsigned_probe,
                    "kind=unsigned size=8",
                    "GVAA INT8U evidence",
                )

                structural_probe = run_probe(
                    read_probe, port, "XCBR1$ST$Health$stVal"
                )
                require_probe(
                    structural_probe,
                    "value=false",
                    "structural-only leaf read",
                )

                # The SCL ReportControl is part of the P0 structural model even
                # though RptEna/GI/report emission are intentionally deferred to P2.
                rcb_probe = run_probe(
                    read_probe, port, "LLN0$RP$urcb01$RptID"
                )
                require_probe(
                    rcb_probe,
                    "value=MU01_LD0_URCB01",
                    "URCB RptID read",
                )
                rcb_dataset_probe = run_probe(
                    read_probe, port, "LLN0$RP$urcb01$DatSet"
                )
                require_probe(
                    rcb_dataset_probe,
                    "value=MU01LD0/LLN0$dsSV",
                    "URCB DataSet binding read",
                )

                app.wait(timeout=32)
                print(
                    "IEDSIM_P0_FULL_MODEL_PASS "
                    "edited=MU01LD0/TCTR1$MX$Amp$instMag$i:42 "
                    "gvaa=integer:16,unsigned:8 dataset=LLN0$dsSV "
                    "rcb=LLN0$RP$urcb01 structural=MU01LD0/XCBR1$ST$Health$stVal:false"
                )
                return 0
            raise RuntimeError(last_error)
        except BaseException as exception:
            last_error = str(exception) or last_error
            if app.poll() is None:
                try:
                    app.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=5)
            app_log.seek(0)
            output = app_log.read().strip()
            raise RuntimeError(
                f"GUI full-model/live-value test failed: {last_error}; app_output={output}"
            ) from exception


if __name__ == "__main__":
    raise SystemExit(main())