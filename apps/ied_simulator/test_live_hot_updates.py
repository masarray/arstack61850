#!/usr/bin/env python3
"""Milestone-J hot update data-plane regression and bounded burst evidence."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time

BURST_RE = re.compile(
    r"IEDSIM_LIVE_BURST\s+requested=(?P<requested>\d+)\s+accepted=(?P<accepted>\d+)\s+"
    r"coalesced=(?P<coalesced>\d+)\s+rejected=(?P<rejected>\d+)\s+"
    r"pending=(?P<pending>\d+)\s+pending_max=(?P<pending_max>\d+)\s+final=(?P<final>\S+)"
)
ACK_RE = re.compile(
    r"IEDSIM_LIVE_ACK\s+generation=(?P<generation>\d+)\s+revision=(?P<revision>\d+)\s+"
    r"latency_ms=(?P<latency>\d+)\s+pending=(?P<pending>\d+)\s+inflight=(?P<inflight>\d+)"
)


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def creation_flags() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def resolve_probe(argument: str) -> str:
    path = Path(argument)
    names = {"ariec61850_mms_read_probe", "ariec61850_mms_read_probe.exe"}
    if path.is_file() and path.name in names:
        return str(path.resolve())
    if path.is_dir():
        matches = sorted(
            candidate for candidate in path.rglob("ariec61850_mms_read_probe*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0].resolve())
    raise FileNotFoundError(f"MMS read probe not found under {path}")


def read_value(probe: str, port: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            probe,
            "127.0.0.1",
            str(port),
            "--domain",
            "MU01LD0",
            "--item",
            "TCTR1$MX$Amp$instMag$i",
            "--timeout-ms",
            "2500",
        ],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
        creationflags=creation_flags(),
    )


def wait_for_value(probe: str, port: int, expected: str, deadline_s: float) -> str:
    deadline = time.monotonic() + deadline_s
    last = ""
    while time.monotonic() < deadline:
        result = read_value(probe, port)
        last = result.stdout + result.stderr
        if result.returncode == 0 and f"value={expected}" in result.stdout:
            return result.stdout.strip()
        time.sleep(0.04)
    raise RuntimeError(f"MMS never exposed value={expected}; last probe output={last!r}")


def wait_for_manifest(pid: int, deadline_s: float = 8.0) -> Path:
    path = Path(tempfile.gettempdir()) / f"arstack-ied-simulator-{pid}.model"
    deadline = time.monotonic() + deadline_s
    while time.monotonic() < deadline:
        if path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            if text.startswith("ARSTACK_IED_MODEL\t2\t1\n"):
                return path
        time.sleep(0.03)
    raise RuntimeError(f"startup manifest not created at {path}")


def run_case(app: Path, probe: str, scl: Path, updates: int) -> None:
    port = free_port()
    environment = os.environ.copy()
    environment["QT_QPA_PLATFORM"] = "offscreen"
    environment["ARSTACK_IEDSIM_QA"] = "1"

    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log:
        process = subprocess.Popen(
            [
                str(app),
                "--scl",
                str(scl),
                "--port",
                str(port),
                "--runtime",
                "--qa-live-burst",
                str(updates),
                "--exit-after-ms",
                "12000",
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            env=environment,
            creationflags=creation_flags(),
        )
        manifest = wait_for_manifest(process.pid)
        try:
            expected = str(updates - 1)
            read_output = wait_for_value(probe, port, expected, 8.0)
            manifest_text = manifest.read_text(encoding="utf-8", errors="replace")
            if not manifest_text.startswith("ARSTACK_IED_MODEL\t2\t1\n"):
                raise RuntimeError("hot update rewrote the startup manifest revision")
            mapped = "TCTR1$MX$Amp$instMag$i\tINT32\tNumber\t0"
            if mapped not in manifest_text:
                raise RuntimeError(
                    "hot update unexpectedly rewrote the startup OBJ value instead of using the delta channel"
                )

            process.wait(timeout=15)
            if process.returncode != 0:
                raise RuntimeError(f"simulator exited {process.returncode}")
            log.flush()
            log.seek(0)
            output = log.read()
            bursts = list(BURST_RE.finditer(output))
            acks = list(ACK_RE.finditer(output))
            if not bursts:
                raise RuntimeError(f"missing IEDSIM_LIVE_BURST evidence; output={output[-6000:]}")
            burst = bursts[-1]
            metrics = {key: int(value) for key, value in burst.groupdict().items() if key != "final"}
            if metrics["requested"] != updates or metrics["accepted"] != updates:
                raise RuntimeError(f"live burst was not fully accepted: {metrics}")
            if metrics["rejected"] != 0:
                raise RuntimeError(f"live burst rejected updates: {metrics}")
            if metrics["pending_max"] > 256:
                raise RuntimeError(f"live pending queue exceeded bound: {metrics}")
            if burst.group("final") != expected:
                raise RuntimeError(
                    f"live burst final value mismatch: {burst.group('final')} != {expected}"
                )
            if updates >= 1000 and metrics["coalesced"] < updates - 64:
                raise RuntimeError(
                    f"expected same-point burst to coalesce heavily, got {metrics['coalesced']} of {updates}"
                )
            if not acks:
                raise RuntimeError(f"missing IEDSIM_LIVE_ACK evidence; output={output[-6000:]}")
            max_latency = max(int(match.group("latency")) for match in acks)
            max_inflight = max(int(match.group("inflight")) for match in acks)
            if max_latency > 1500:
                raise RuntimeError(f"live ACK latency exceeded 1500 ms: {max_latency}")
            if max_inflight > 256:
                raise RuntimeError(f"live in-flight ACK tracking exceeded bound: {max_inflight}")
            if "kind=live_update_ack" not in output or "accepted=true" not in output:
                raise RuntimeError("server did not acknowledge the live update")

            print(
                "LIVE_HOT_UPDATE_CASE "
                f"updates={updates} coalesced={metrics['coalesced']} "
                f"pending_max={metrics['pending_max']} max_ack_ms={max_latency} "
                f"max_inflight={max_inflight} manifest_revision=1 final={expected} "
                f"mms_read={read_output.splitlines()[0] if read_output else 'ok'}"
            )
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=3)
            try:
                manifest.unlink()
            except FileNotFoundError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--read-probe", required=True)
    parser.add_argument("--scl", required=True, type=Path)
    parser.add_argument("--bursts", default="1000,10000")
    args = parser.parse_args()

    app = args.app.resolve()
    scl = args.scl.resolve()
    probe = resolve_probe(args.read_probe)
    if not app.is_file():
        raise FileNotFoundError(f"simulator not found: {app}")
    if not scl.is_file():
        raise FileNotFoundError(f"SCL fixture not found: {scl}")

    bursts = [int(token) for token in args.bursts.split(",") if token.strip()]
    if not bursts or any(value <= 0 or value > 100000 for value in bursts):
        raise ValueError("--bursts values must be 1..100000")
    for updates in bursts:
        run_case(app, probe, scl, updates)
    print(
        "LIVE_RUNTIME_DATA_PLANE_PASS "
        f"cases={','.join(str(value) for value in bursts)} "
        "manifest_hot_rewrites=0 bounded_pending=256 bounded_inflight=256"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
