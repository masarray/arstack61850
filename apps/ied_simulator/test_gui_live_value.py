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
            candidate for candidate in path.rglob("ariec61850_mms_read_probe*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0])
    raise FileNotFoundError(f"MMS read probe not found under {path}")


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
                "12000",
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
            manifest_deadline = time.monotonic() + 6.0
            while time.monotonic() < manifest_deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                try:
                    manifest_text = manifest_path.read_text(encoding="utf-8")
                except (FileNotFoundError, PermissionError, UnicodeDecodeError):
                    manifest_text = ""
                gui_value_present = (
                    "TCTR1$MX$Amp$instMag$i\tINT32\tNumber\t42" in manifest_text
                )
                # XCBR1.Health is deliberately absent from every DataSet. Its presence
                # proves DataTypeTemplates materialization, not FCDA-driven projection.
                structural_only_leaf_present = (
                    "XCBR1$ST$Health$stVal\tBOOLEAN\tBoolean\tfalse" in manifest_text
                )
                if (
                    manifest_text.startswith("ARSTACK_IED_MODEL\t3\t2\n")
                    and gui_value_present
                    and structural_only_leaf_present
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError(
                    "GUI did not publish v3 revision 2 with both edited and structural-only leaves"
                )

            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                time.sleep(0.25)
                probe = subprocess.run(
                    [
                        read_probe,
                        "127.0.0.1",
                        str(port),
                        "--domain",
                        "MU01LD0",
                        "--item",
                        "TCTR1$MX$Amp$instMag$i",
                        "--timeout-ms",
                        "3000",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=6,
                    check=False,
                    creationflags=creation_flags(),
                )
                if probe.returncode == 0 and "value=42" in probe.stdout:
                    structural_probe = subprocess.run(
                        [
                            read_probe,
                            "127.0.0.1",
                            str(port),
                            "--domain",
                            "MU01LD0",
                            "--item",
                            "XCBR1$ST$Health$stVal",
                            "--timeout-ms",
                            "3000",
                        ],
                        capture_output=True,
                        text=True,
                        timeout=6,
                        check=False,
                        creationflags=creation_flags(),
                    )
                    if structural_probe.returncode == 0 and "value=false" in structural_probe.stdout:
                        app.wait(timeout=14)
                        print(
                            "IEDSIM_FULL_MODEL_LIVE_VALUE_PASS "
                            "edited=MU01LD0/TCTR1$MX$Amp$instMag$i:42 "
                            "structural=MU01LD0/XCBR1$ST$Health$stVal:false"
                        )
                        return 0
                    last_error = (
                        "structural probe "
                        f"exit={structural_probe.returncode} stdout={structural_probe.stdout} "
                        f"stderr={structural_probe.stderr}"
                    )
                else:
                    last_error = (
                        f"exit={probe.returncode} stdout={probe.stdout} stderr={probe.stderr}"
                    )
                time.sleep(0.2)
            raise RuntimeError(last_error)
        except BaseException:
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
            )


if __name__ == "__main__":
    raise SystemExit(main())
