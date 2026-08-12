#!/usr/bin/env python3
"""Launch the Qt simulator and prove GUI-applied state is visible over MMS."""

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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--read-probe", required=True)
    parser.add_argument("--scl", required=True)
    args = parser.parse_args()

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
                if manifest_text.startswith("ARSTACK_IED_MODEL\t2\t2\n") and (
                    "TCTR1$MX$Amp$instMag$i\tINT32\tNumber\t42" in manifest_text
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("GUI did not publish manifest revision 2 with value 42")

            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                time.sleep(0.25)
                probe = subprocess.run(
                    [
                        args.read_probe,
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
                    app.wait(timeout=14)
                    print(
                        "IEDSIM_GUI_LIVE_VALUE_PASS "
                        "reference=MU01LD0/TCTR1$MX$Amp$instMag$i value=42"
                    )
                    return 0
                last_error = (
                    f"exit={probe.returncode} stdout={probe.stdout} stderr={probe.stderr}"
                )
                time.sleep(0.2)
            raise RuntimeError(last_error)
        except BaseException:
            if app.poll() is None:
                try:
                    # Let Qt destroy its QProcess child and release the listener.
                    app.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=5)
            app_log.seek(0)
            output = app_log.read().strip()
            raise RuntimeError(
                f"GUI live-value test failed: {last_error}; app_output={output}"
            )


if __name__ == "__main__":
    raise SystemExit(main())
