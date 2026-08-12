#!/usr/bin/env python3
"""Prove SCL-style static DataSets and live values on one MMS association."""

from __future__ import annotations

import argparse
import json
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


def manifest(revision: int, value: bool) -> str:
    text = "true" if value else "false"
    return "\n".join(
        [
            f"ARSTACK_IED_MODEL\t2\t{revision}",
            "LN\tTESTIEDLD0\tLLN0",
            "LN\tTESTIEDLD0\tGGIO1",
            f"OBJ\tTESTIEDLD0\tGGIO1$ST$Ind1$stVal\tBOOLEAN\tBoolean\t{text}",
            "OBJ\tTESTIEDLD0\tGGIO1$ST$Ind2$stVal\tBOOLEAN\tBoolean\tfalse",
            "DS\tTESTIEDLD0\tLLN0$Status\tTESTIEDLD0\tGGIO1$ST$Ind1$stVal",
            "DS\tTESTIEDLD0\tLLN0$Status\tTESTIEDLD0\tGGIO1$ST$Ind2$stVal",
            "",
        ]
    )


def atomic_write(path: Path, text: str) -> None:
    replacement = path.with_suffix(".next")
    replacement.write_text(text, encoding="utf-8", newline="\n")
    os.replace(replacement, path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--discovery", required=True)
    parser.add_argument("--read-probe", required=True)
    args = parser.parse_args()

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-iedsim-runtime-") as directory:
        model = Path(directory) / "runtime.model"
        atomic_write(model, manifest(1, False))
        server = subprocess.Popen(
            [
                args.server,
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--model-manifest",
                str(model),
                "--max-connections",
                "2",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            creationflags=creation_flags(),
        )
        try:
            time.sleep(0.35)
            discovery = subprocess.run(
                [
                    args.discovery,
                    "127.0.0.1",
                    str(port),
                    "--model-json",
                    "--timeout-ms",
                    "3000",
                ],
                capture_output=True,
                text=True,
                timeout=15,
                check=False,
                creationflags=creation_flags(),
            )
            if discovery.returncode != 0:
                raise RuntimeError(f"discovery failed: {discovery.stderr}\n{discovery.stdout}")
            document = json.loads(discovery.stdout)
            if document.get("coverage", {}).get("dataSetCount") != 1:
                raise RuntimeError(f"static DataSet missing: {discovery.stdout}")
            data_sets = document.get("dataSets", [])
            if len(data_sets) != 1 or data_sets[0].get("memberCount") != 2:
                raise RuntimeError(f"static DataSet members mismatch: {data_sets}")

            probe = subprocess.Popen(
                [
                    args.read_probe,
                    "127.0.0.1",
                    str(port),
                    "--domain",
                    "TESTIEDLD0",
                    "--item",
                    "GGIO1$ST$Ind1$stVal",
                    "--count",
                    "4",
                    "--delay-ms",
                    "500",
                    "--timeout-ms",
                    "3000",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                creationflags=creation_flags(),
            )
            assert probe.stdout is not None
            first = probe.stdout.readline().strip()
            if "value=false" not in first:
                raise RuntimeError(f"initial MMS value mismatch: {first}")
            atomic_write(model, manifest(2, True))
            remaining_stdout, probe_stderr = probe.communicate(timeout=10)
            reads = [first, *remaining_stdout.splitlines()]
            if probe.returncode != 0 or not any("value=true" in line for line in reads[1:]):
                raise RuntimeError(
                    "live MMS value did not refresh on the existing association:\n"
                    + "\n".join(reads)
                    + f"\nstderr:\n{probe_stderr}"
                )
            server_stdout, server_stderr = server.communicate(timeout=8)
        except BaseException:
            server.kill()
            server_stdout, server_stderr = server.communicate()
            raise

    if server.returncode != 0 or "kind=value_sync" not in server_stdout:
        raise RuntimeError(
            f"server live-value evidence missing (exit={server.returncode}):\n"
            f"{server_stdout}\n{server_stderr}"
        )
    print(
        "IEDSIM_RUNTIME_MODEL_PASS datasets=1 members=2 "
        "valueTransition=false->true associationPreserved=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
