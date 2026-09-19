#!/usr/bin/env python3
"""Large-model external IEC 61850 client discovery parity regression.

Reproduces the failure shape captured from external vendor external IEC 61850 client: a ~5k-object
reference IEC 61850 model-like inventory where GVAA of AA1E1F06R4ESQZ1/CSWI1 must complete
without stalling the association worker.
"""

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
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def build_manifest() -> str:
    lines = ["ARSTACK_IED_MODEL\t2\t1"]
    target = "AA1E1F06R4ESQZ1"
    # Preserve the same root order seen on the golden external IEC 61850 client server.
    lines.extend([
        f"LN\t{target}\tLLN0",
        f"LN\t{target}\tCSWI1",
        f"LN\t{target}\tCILO1",
        f"LN\t{target}\tXSWI1",
        f"OBJ\t{target}\tLLN0$ST$Mod$stVal\tENUM\tEnumeration\t1",
        f"OBJ\t{target}\tLLN0$ST$Mod$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{target}\tLLN0$ST$Mod$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{target}\tCSWI1$ST$Pos$stVal\tDBPOS\tEnumeration\toff",
        f"OBJ\t{target}\tCSWI1$ST$Pos$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{target}\tCSWI1$ST$Pos$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{target}\tCSWI1$ST$Pos$stSeld\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{target}\tCSWI1$CF$Pos$ctlModel\tENUM\tEnumeration\t4",
        f"OBJ\t{target}\tCSWI1$CF$Pos$sboTimeout\tINT32U\tNumber\t10000",
        f"OBJ\t{target}\tCSWI1$CF$Pos$operTimeout\tINT32U\tNumber\t10000",
        f"OBJ\t{target}\tCSWI1$DC$NamPlt$vendor\tVisString255\tString\tARStack",
        f"OBJ\t{target}\tCSWI1$EX$EEHealth$stVal\tENUM\tEnumeration\t1",
        f"OBJ\t{target}\tCSWI1$OR$OpCnt$stVal\tINT32U\tNumber\t0",
        f"OBJ\t{target}\tCILO1$ST$EnaOpn$stVal\tBOOLEAN\tBoolean\ttrue",
        f"OBJ\t{target}\tXSWI1$ST$Pos$stVal\tDBPOS\tEnumeration\toff",
        f"CTL\t{target}\tCSWI1\tPos\tDPC\t4",
    ])
    # Make CSWI1 non-trivial while keeping the same functional-constraint shape.
    for index in range(1, 81):
        lines.extend([
            f"OBJ\t{target}\tCSWI1$ST$Ind{index}$stVal\tBOOLEAN\tBoolean\tfalse",
            f"OBJ\t{target}\tCSWI1$ST$Ind{index}$q\tQUALITY\tQuality\tgood",
            f"OBJ\t{target}\tCSWI1$ST$Ind{index}$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        ])

    # Inflate the global object table to the production-scale range that exposed
    # the pathological repeated full-table scans.
    for ld in range(1, 32):
        domain = f"AA1E1F06R4LD{ld:02d}"
        lines.append(f"LN\t{domain}\tGGIO1")
        for index in range(1, 51):
            lines.extend([
                f"OBJ\t{domain}\tGGIO1$ST$Ind{index}$stVal\tBOOLEAN\tBoolean\tfalse",
                f"OBJ\t{domain}\tGGIO1$ST$Ind{index}$q\tQUALITY\tQuality\tgood",
                f"OBJ\t{domain}\tGGIO1$ST$Ind{index}$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
            ])
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--discovery", required=True)
    args = parser.parse_args()

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-interop-full-discovery-") as directory:
        root = Path(directory)
        model = root / "large.model"
        model.write_text(build_manifest(), encoding="utf-8", newline="\n")
        stdout_path = root / "server.stdout.log"
        stderr_path = root / "server.stderr.log"

        with stdout_path.open("w+", encoding="utf-8") as server_out, \
             stderr_path.open("w+", encoding="utf-8") as server_err:
            server = subprocess.Popen(
                [
                    args.server,
                    "--host", "127.0.0.1",
                    "--port", str(port),
                    "--model-manifest", str(model),
                    "--max-connections", "4",
                    "--max-active", "4",
                ],
                stdout=server_out,
                stderr=server_err,
                text=True,
                creationflags=creation_flags(),
            )
            try:
                time.sleep(0.4)
                started = time.monotonic()
                discovery = subprocess.run(
                    [
                        args.discovery,
                        "127.0.0.1",
                        str(port),
                        "--model-json",
                        "--no-datasets",
                        "--no-rcb",
                        "--max-types", "128",
                        "--timeout-ms", "2500",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=12,
                    check=False,
                    creationflags=creation_flags(),
                )
                elapsed_ms = (time.monotonic() - started) * 1000.0
                if discovery.returncode != 0:
                    raise RuntimeError(
                        "large-model discovery failed: "
                        f"exit={discovery.returncode} elapsed_ms={elapsed_ms:.1f}\n"
                        f"stdout:\n{discovery.stdout}\nstderr:\n{discovery.stderr}"
                    )
                document = json.loads(discovery.stdout)
                coverage = document.get("coverage", {})
                if coverage.get("logicalDeviceCount") != 32:
                    raise RuntimeError(f"logical-device coverage mismatch: {coverage}")
                serialized = discovery.stdout
                for required in [
                    "AA1E1F06R4ESQZ1",
                    "CSWI1",
                    "CILO1",
                    "XSWI1",
                ]:
                    if required not in serialized:
                        raise RuntimeError(
                            f"golden discovery identity {required!r} missing"
                        )
                # Golden external IEC 61850 client answers the captured CSWI1 GVAA in ~61 ms.
                # CI is intentionally looser, but a multi-second per-root stall
                # must fail this gate.
                if elapsed_ms > 8000.0:
                    raise RuntimeError(
                        f"large-model discovery exceeded bounded budget: {elapsed_ms:.1f} ms"
                    )
            finally:
                server.kill()
                server.wait(timeout=8)

        server_stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
        server_stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
        if "kind=server_ready" not in server_stdout or "stage=mms" not in server_stdout:
            raise RuntimeError(
                "server lifecycle evidence missing:\n"
                + server_stdout + "\n" + server_stderr
            )
        if "kind=client_error" in server_stdout or "protocol_error" in server_stdout:
            raise RuntimeError(
                "server reported an error during full discovery:\n"
                + server_stdout + "\n" + server_stderr
            )

        print(
            "INTEROP_FULL_DISCOVERY_GOLDEN_PASS "
            "objects=~4900 logicalDevices=32 target=AA1E1F06R4ESQZ1/CSWI1 "
            f"elapsed_ms={elapsed_ms:.1f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
