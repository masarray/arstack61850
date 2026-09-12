#!/usr/bin/env python3
"""Same-process start/stop/restart soak for the Qt IED simulator runtime."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import socket
import subprocess


def reserve_ephemeral_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--scl", required=True, type=Path)
    parser.add_argument("--cycles", type=int, default=12)
    args = parser.parse_args()

    app = args.app.resolve()
    scl = args.scl.resolve()
    if not app.is_file():
        raise SystemExit(f"simulator executable not found: {app}")
    if not scl.is_file():
        raise SystemExit(f"SCL fixture not found: {scl}")
    if args.cycles < 1:
        raise SystemExit("--cycles must be positive")

    port = reserve_ephemeral_port()
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    completed = subprocess.run(
        [
            str(app),
            "--scl",
            str(scl),
            "--port",
            str(port),
            "--qa-runtime-cycles",
            str(args.cycles),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
        timeout=50,
        check=False,
    )
    output = completed.stdout or ""
    marker = f"RUNTIME_CYCLE_SOAK_PASS cycles= {args.cycles}"
    if completed.returncode != 0 or marker not in output:
        raise RuntimeError(
            "runtime lifecycle soak failed: "
            f"exit={completed.returncode} port={port} output={output[-6000:]}"
        )

    started = output.count("RUNTIME_CYCLE_STARTED")
    finished = output.count("RUNTIME_CYCLE_FINISHED")
    if started != args.cycles or finished != args.cycles:
        raise RuntimeError(
            f"expected {args.cycles} complete cycles; started={started} finished={finished}; "
            f"output={output[-6000:]}"
        )
    if args.cycles >= 2 and "RUNTIME_RESTART_GUARD_PASS" not in output:
        raise RuntimeError(
            "restart generation guard was not exercised beyond the delayed-kill window; "
            f"output={output[-6000:]}"
        )

    print(
        f"RUNTIME_LIFECYCLE_PASS cycles={args.cycles} port={port} "
        f"started={started} finished={finished} delayed_kill_guard=pass",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
