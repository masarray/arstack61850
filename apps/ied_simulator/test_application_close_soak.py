#!/usr/bin/env python3
"""Release-candidate application-close soak.

Launches the real Qt workbench with one MMS simulator child, waits until the
configured TCP endpoint is accepting connections, lets the application exit
normally, then verifies that no helper process using that port survives and
that the listen port can be rebound. This covers the aboutToQuit teardown path
rather than only the explicit Stop button path.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import socket
import subprocess
import sys
import time
from typing import Iterable


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_until_listening(port: int, process: subprocess.Popen[str], timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return False
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.settimeout(0.15)
            if probe.connect_ex(("127.0.0.1", port)) == 0:
                return True
        time.sleep(0.03)
    return False


def linux_server_processes_for_port(port: int) -> list[tuple[int, str]]:
    proc = pathlib.Path("/proc")
    if not proc.is_dir():
        return []
    needle = str(port)
    matches: list[tuple[int, str]] = []
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            raw = (entry / "cmdline").read_bytes()
        except (FileNotFoundError, PermissionError, ProcessLookupError, OSError):
            continue
        if not raw:
            continue
        parts = [part.decode("utf-8", errors="replace") for part in raw.split(b"\0") if part]
        joined = " ".join(parts)
        if "ariec61850_ied_simulator_server" not in joined:
            continue
        for index, part in enumerate(parts[:-1]):
            if part == "--port" and parts[index + 1] == needle:
                matches.append((int(entry.name), joined))
                break
    return matches


def port_rebindable(port: int) -> bool:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False


def wait_cleanup(port: int, timeout: float) -> tuple[bool, list[tuple[int, str]]]:
    deadline = time.monotonic() + timeout
    last_processes: list[tuple[int, str]] = []
    while time.monotonic() < deadline:
        last_processes = linux_server_processes_for_port(port)
        if not last_processes and port_rebindable(port):
            return True, []
        time.sleep(0.05)
    return False, last_processes


def tail(text: str, lines: int = 30) -> str:
    return "\n".join(text.splitlines()[-lines:])


def run_case(app: pathlib.Path, scl: pathlib.Path, cycle: int) -> None:
    port = reserve_port()
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    command = [
        str(app),
        "--scl",
        str(scl),
        "--port",
        str(port),
        "--runtime",
        "--exit-after-ms",
        "1600",
    ]
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )
    if not wait_until_listening(port, process, timeout=5.0):
        output, _ = process.communicate(timeout=8.0)
        raise RuntimeError(
            f"cycle {cycle}: runtime never listened on 127.0.0.1:{port}; rc={process.returncode}\n{tail(output)}"
        )

    try:
        output, _ = process.communicate(timeout=10.0)
    except subprocess.TimeoutExpired:
        process.kill()
        output, _ = process.communicate(timeout=3.0)
        raise RuntimeError(f"cycle {cycle}: application did not exit normally\n{tail(output)}")

    if process.returncode != 0:
        raise RuntimeError(
            f"cycle {cycle}: application exited with {process.returncode}\n{tail(output)}"
        )

    cleaned, survivors = wait_cleanup(port, timeout=3.0)
    if not cleaned:
        survivor_text = "; ".join(f"pid={pid} {cmd}" for pid, cmd in survivors) or "none-visible"
        raise RuntimeError(
            f"cycle {cycle}: child/socket cleanup failed for port {port}; survivors={survivor_text}"
        )


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=pathlib.Path)
    parser.add_argument("--scl", required=True, type=pathlib.Path)
    parser.add_argument("--cycles", type=int, default=12)
    args = parser.parse_args(argv)

    if args.cycles < 1 or args.cycles > 50:
        parser.error("--cycles must be between 1 and 50")
    if not args.app.is_file():
        parser.error(f"application not found: {args.app}")
    if not args.scl.is_file():
        parser.error(f"SCL fixture not found: {args.scl}")

    try:
        for cycle in range(1, args.cycles + 1):
            run_case(args.app.resolve(), args.scl.resolve(), cycle)
    except RuntimeError as exc:
        print(f"APPLICATION_CLOSE_SOAK_FAIL {exc}", file=sys.stderr)
        return 1

    print(
        "APPLICATION_CLOSE_SOAK_PASS"
        f" cycles={args.cycles}"
        " runtime_started=pass"
        " about_to_quit_cleanup=pass"
        " orphan_children=0"
        " socket_release=pass"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
