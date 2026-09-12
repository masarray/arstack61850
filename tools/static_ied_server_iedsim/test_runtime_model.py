#!/usr/bin/env python3
"""Prove structured SCL-style static DataSets and live values on one MMS association."""

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
    """Build a vendor-style model with configured whole-DO DataSet members.

    The 36/22 cardinalities mirror the SIPROTEC acceptance case that exposed the
    regression.  Each configured member owns multiple leaves, so flattening the
    Digital DataSet would exceed the 64-member static DataSet limit while the
    correct configured membership remains safely at 36.
    """
    tracked_value = "true" if value else "false"
    lines = [
        f"ARSTACK_IED_MODEL\t2\t{revision}",
        "LN\tTESTIEDLD0\tLLN0",
        "LN\tTESTIEDLD0\tGGIO1",
    ]

    for index in range(1, 37):
        object_name = f"Digital{index}"
        st_val = tracked_value if index == 1 else "false"
        lines.extend(
            [
                f"OBJ\tTESTIEDLD0\tGGIO1$ST${object_name}$stVal\tBOOLEAN\tBoolean\t{st_val}",
                f"OBJ\tTESTIEDLD0\tGGIO1$ST${object_name}$q\tBOOLEAN\tBoolean\tfalse",
                f"OBJ\tTESTIEDLD0\tGGIO1$ST${object_name}$detail\tBOOLEAN\tBoolean\tfalse",
                f"DS\tTESTIEDLD0\tLLN0$Digital\tTESTIEDLD0\tGGIO1$ST${object_name}",
            ]
        )

    for index in range(1, 23):
        object_name = f"Analog{index}"
        lines.extend(
            [
                f"OBJ\tTESTIEDLD0\tGGIO1$MX${object_name}$mag\tINTEGER\tInt32\t{index}",
                f"OBJ\tTESTIEDLD0\tGGIO1$MX${object_name}$q\tBOOLEAN\tBoolean\tfalse",
                f"OBJ\tTESTIEDLD0\tGGIO1$MX${object_name}$range\tINTEGER\tInt32\t0",
                f"DS\tTESTIEDLD0\tLLN0$Analog\tTESTIEDLD0\tGGIO1$MX${object_name}",
            ]
        )

    lines.append("")
    return "\n".join(lines)


def atomic_write(path: Path, text: str) -> None:
    replacement = path.with_suffix(".next")
    replacement.write_text(text, encoding="utf-8", newline="\n")
    os.replace(replacement, path)


def run_read_probe(
    executable: str,
    port: int,
    item: str,
    *,
    count: int = 1,
    delay_ms: int = 0,
) -> subprocess.CompletedProcess[str]:
    command = [
        executable,
        "127.0.0.1",
        str(port),
        "--domain",
        "TESTIEDLD0",
        "--item",
        item,
        "--count",
        str(count),
        "--timeout-ms",
        "3000",
    ]
    if delay_ms:
        command.extend(["--delay-ms", str(delay_ms)])
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
        creationflags=creation_flags(),
    )


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
                "3",
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
            if document.get("coverage", {}).get("dataSetCount") != 2:
                raise RuntimeError(f"static DataSets missing: {discovery.stdout}")
            data_sets = document.get("dataSets", [])
            member_counts = sorted(data_set.get("memberCount") for data_set in data_sets)
            if len(data_sets) != 2 or member_counts != [22, 36]:
                raise RuntimeError(
                    "configured whole-DO DataSet cardinalities changed: "
                    f"{data_sets}"
                )

            # A whole-DO FCDA must be exposed as a readable MMS STRUCTURE.  A
            # successful external read proves the synthesized object is present
            # in the static object table instead of being silently dropped.
            structured = run_read_probe(
                args.read_probe,
                port,
                "GGIO1$ST$Digital1",
            )
            if structured.returncode != 0 or "value=" not in structured.stdout:
                raise RuntimeError(
                    "whole-DO MMS STRUCTURE read failed: "
                    f"exit={structured.returncode} stdout={structured.stdout!r} "
                    f"stderr={structured.stderr!r}"
                )

            probe = subprocess.Popen(
                [
                    args.read_probe,
                    "127.0.0.1",
                    str(port),
                    "--domain",
                    "TESTIEDLD0",
                    "--item",
                    "GGIO1$ST$Digital1$stVal",
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

    if (
        server.returncode != 0
        or "kind=server_ready" not in server_stdout
        or "kind=value_sync" not in server_stdout
    ):
        raise RuntimeError(
            f"server structured/live-value evidence missing (exit={server.returncode}):\n"
            f"{server_stdout}\n{server_stderr}"
        )
    print(
        "IEDSIM_RUNTIME_MODEL_PASS datasets=2 digitalMembers=36 analogMembers=22 "
        "wholeDoStructureReadable=true valueTransition=false->true "
        "associationPreserved=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
