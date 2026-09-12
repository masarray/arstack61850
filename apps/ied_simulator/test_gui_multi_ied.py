#!/usr/bin/env python3
"""Prove the Qt simulator can run two independent IED endpoints simultaneously."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def creation_flags() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def resolve_read_probe(argument: str) -> str:
    path = Path(argument)
    names = {"ariec61850_mms_read_probe", "ariec61850_mms_read_probe.exe"}
    if path.is_file() and path.name in names:
        return str(path)
    if path.is_dir():
        matches = sorted(
            candidate
            for candidate in path.rglob("ariec61850_mms_read_probe*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0])
    raise FileNotFoundError(f"MMS read probe not found under {path}")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def read_point(
    read_probe: str,
    host: str,
    port: int,
    domain: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            read_probe,
            host,
            str(port),
            "--domain",
            domain,
            "--item",
            "TCTR1$MX$AmpSv$instMag$i",
            "--timeout-ms",
            "1600",
        ],
        capture_output=True,
        text=True,
        timeout=4,
        check=False,
        creationflags=creation_flags(),
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
                "--ied-endpoint",
                f"0=127.0.0.1:{port}",
                "--ied-endpoint",
                f"1=127.0.0.2:{port}",
                "--start-ied",
                "0",
                "--start-ied",
                "1",
                "--exit-after-ms",
                "25000",
            ],
            stdout=app_log,
            stderr=subprocess.STDOUT,
            text=True,
            env=environment,
            creationflags=creation_flags(),
        )
        try:
            deadline = time.monotonic() + 12.0
            first: subprocess.CompletedProcess[str] | None = None
            second: subprocess.CompletedProcess[str] | None = None
            while time.monotonic() < deadline:
                if app.poll() is not None:
                    break
                first = read_point(
                    read_probe,
                    "127.0.0.1",
                    port,
                    "MU01_4I_4V_1MU01",
                )
                second = read_point(
                    read_probe,
                    "127.0.0.2",
                    port,
                    "MU01_4I_4V_2MU01",
                )
                first_ok = first.returncode == 0 and "value=0" in first.stdout
                second_ok = second.returncode == 0 and "value=0" in second.stdout
                if first_ok and second_ok:
                    print(
                        "MULTI_IED_PASS "
                        f"endpoint0=127.0.0.1:{port} domain0=MU01_4I_4V_1MU01 "
                        f"endpoint1=127.0.0.2:{port} domain1=MU01_4I_4V_2MU01"
                    )
                    return 0
                time.sleep(0.25)

            app_log.seek(0)
            output = app_log.read()
            raise RuntimeError(
                "two-IED same-port/different-IP regression failed: "
                f"app_exit={app.poll()} first={None if first is None else (first.returncode, first.stdout, first.stderr)} "
                f"second={None if second is None else (second.returncode, second.stdout, second.stderr)} "
                f"app_output={output!r}"
            )
        finally:
            if app.poll() is None:
                app.terminate()
                try:
                    app.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=3)


if __name__ == "__main__":
    raise SystemExit(main())
