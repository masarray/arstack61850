#!/usr/bin/env python3
"""Negative-path evidence for the generation/revision guarded live data plane."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def resolve_executable(argument: str, names: set[str]) -> str:
    path = Path(argument)
    if path.is_file() and path.name in names:
        return str(path.resolve())
    if path.is_dir():
        matches = sorted(
            candidate for candidate in path.rglob("*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0].resolve())
    raise FileNotFoundError(f"executable {sorted(names)} not found under {path}")


def encode_line(generation: int, revision: int, domain: str, item: str, value: str) -> str:
    return "\t".join(
        [
            "ARSTACK_LIVE",
            "1",
            str(generation),
            str(revision),
            domain.encode().hex(),
            item.encode().hex(),
            value.encode().hex(),
        ]
    ) + "\n"


def read_until(process: subprocess.Popen[str], needle: str, timeout_s: float) -> str:
    if process.stdout is None:
        raise RuntimeError("server stdout unavailable")
    deadline = time.monotonic() + timeout_s
    output = ""
    while time.monotonic() < deadline:
        if process.poll() is not None:
            output += process.stdout.read() or ""
            break
        ready, _, _ = select.select([process.stdout], [], [], 0.05)
        if not ready:
            continue
        line = process.stdout.readline()
        if not line:
            continue
        output += line
        if needle in output:
            return output
    raise RuntimeError(f"timed out waiting for {needle!r}; output={output[-4000:]}")


def run_probe(probe: str, port: int, expected: str) -> str:
    completed = subprocess.run(
        [
            probe,
            "127.0.0.1",
            str(port),
            "--domain",
            "TESTLD0",
            "--item",
            "LLN0$ST$Mod$stVal",
            "--timeout-ms",
            "2500",
        ],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )
    output = (completed.stdout or "") + (completed.stderr or "")
    if completed.returncode != 0 or f"value={expected}" not in completed.stdout:
        raise RuntimeError(f"MMS value mismatch; expected {expected}; output={output!r}")
    return completed.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--read-probe", required=True)
    args = parser.parse_args()

    server = resolve_executable(
        args.server,
        {"ariec61850_ied_simulator_server", "ariec61850_ied_simulator_server.exe"},
    )
    probe = resolve_executable(
        args.read_probe,
        {"ariec61850_mms_read_probe", "ariec61850_mms_read_probe.exe"},
    )
    port = free_port()
    domain = "TESTLD0"
    item = "LLN0$ST$Mod$stVal"

    with tempfile.TemporaryDirectory(prefix="arstack-live-negative-") as temporary:
        manifest = Path(temporary) / "model.tsv"
        manifest.write_text(
            "ARSTACK_IED_MODEL\t2\t1\n"
            "LN\tTESTLD0\tLLN0\n"
            "OBJ\tTESTLD0\tLLN0$ST$Mod$stVal\tBOOLEAN\tBoolean\tfalse\n",
            encoding="utf-8",
        )
        process = subprocess.Popen(
            [
                server,
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--model-manifest",
                str(manifest),
                "--live-stdin",
                "--live-generation",
                "7",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        try:
            output = read_until(process, "kind=server_ready", 5.0)
            if process.stdin is None:
                raise RuntimeError("server stdin unavailable")

            process.stdin.write(encode_line(6, 1, domain, item, "true"))
            process.stdin.flush()
            output += read_until(process, "accepted=false", 3.0)
            if "reason=stale-generation-or-revision" not in output:
                raise RuntimeError(f"stale generation was not rejected; output={output[-4000:]}")
            run_probe(probe, port, "false")

            process.stdin.write(encode_line(7, 1, domain, item, "true"))
            process.stdin.flush()
            output += read_until(process, "revision=1 accepted=true", 3.0)
            run_probe(probe, port, "true")

            process.stdin.write(encode_line(7, 1, domain, item, "false"))
            process.stdin.flush()
            output += read_until(process, "revision=1 accepted=false", 3.0)
            if output.count("reason=stale-generation-or-revision") < 2:
                raise RuntimeError(f"duplicate revision was not rejected; output={output[-4000:]}")
            run_probe(probe, port, "true")

            print(
                "LIVE_DATA_PLANE_NEGATIVE_PASS "
                "stale_generation=rejected duplicate_revision=rejected "
                "valid_revision=accepted stale_state_not_applied=true"
            )
            return 0
        finally:
            if process.stdin is not None:
                try:
                    process.stdin.close()
                except OSError:
                    pass
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)


if __name__ == "__main__":
    raise SystemExit(main())
