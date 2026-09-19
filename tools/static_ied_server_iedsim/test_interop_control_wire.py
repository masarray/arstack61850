#!/usr/bin/env python3
"""Real external vendor external IEC 61850 client SBO-enhanced wire regression.

Locks the observed external-client behavior:
- ctlNum = 0 is accepted on SBOw and Oper;
- Oper may use a fresh T relative to SBOw;
- Check requests synchro + interlock;
- positive CommandTermination precedes process feedback.
"""

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
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def manifest() -> str:
    return "\n".join(
        [
            "ARSTACK_IED_MODEL\t2\t1",
            "LN\tAA1E1F06R4Q0\tCSWI1",
            "OBJ\tAA1E1F06R4Q0\tCSWI1$ST$Pos$stVal\tDBPOS\tEnumeration\toff",
            "OBJ\tAA1E1F06R4Q0\tCSWI1$ST$Pos$q\tQUALITY\tQuality\tgood",
            "OBJ\tAA1E1F06R4Q0\tCSWI1$ST$Pos$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
            "OBJ\tAA1E1F06R4Q0\tCSWI1$CF$Pos$ctlModel\tENUM\tEnumeration\t4",
            "CTL\tAA1E1F06R4Q0\tCSWI1\tPos\tDPC\t4",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--wire-probe", required=True)
    args = parser.parse_args()

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-interop-control-wire-") as directory:
        root = Path(directory)
        model = root / "interop-control.model"
        model.write_text(manifest(), encoding="utf-8", newline="\n")
        stdout_path = root / "server.stdout.log"
        stderr_path = root / "server.stderr.log"

        with stdout_path.open("w+", encoding="utf-8") as out, \
             stderr_path.open("w+", encoding="utf-8") as err:
            server = subprocess.Popen(
                [
                    args.server,
                    "--host", "127.0.0.1",
                    "--port", str(port),
                    "--model-manifest", str(model),
                    "--max-connections", "4",
                    "--max-active", "4",
                ],
                stdout=out,
                stderr=err,
                text=True,
                creationflags=creation_flags(),
            )
            try:
                time.sleep(0.35)
                probe = subprocess.run(
                    [
                        args.wire_probe,
                        "127.0.0.1",
                        str(port),
                        "AA1E1F06R4Q0",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=12,
                    check=False,
                    creationflags=creation_flags(),
                )
                if probe.returncode != 0:
                    raise RuntimeError(
                        f"external IEC 61850 client wire probe failed (exit={probe.returncode}):\n"
                        f"stdout:\n{probe.stdout}\nstderr:\n{probe.stderr}"
                    )
                if "INTEROP_ZERO_CTLNUM_CONTROL_PASS" not in probe.stdout:
                    raise RuntimeError(
                        "wire probe did not emit the expected pass marker:\n"
                        + probe.stdout
                    )
                # Give the server one bounded scheduling turn to emit feedback
                # after positive termination.
                time.sleep(0.10)
            finally:
                server.kill()
                server.wait(timeout=8)

        server_stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
        server_stderr = stderr_path.read_text(encoding="utf-8", errors="replace")

        if "DataAccessError=11" in server_stdout or "protocol_error" in server_stdout:
            raise RuntimeError(
                "server rejected the real external IEC 61850 client-compatible command:\n"
                + server_stdout + "\n" + server_stderr
            )

        termination_index = server_stdout.find("kind=command_termination")
        feedback_index = server_stdout.find("kind=control_process_feedback")
        if (
            termination_index < 0
            or feedback_index < 0
            or termination_index >= feedback_index
            or "positive=true" not in server_stdout
            or "value=on" not in server_stdout
        ):
            raise RuntimeError(
                "server did not preserve positive termination before process feedback:\n"
                + server_stdout + "\n" + server_stderr
            )

        print(
            "INTEROP_REAL_CONTROL_GOLDEN_PASS "
            "ctlNum=0 freshOperT=true "
            "sequence=SBOw,Oper,CommandTermination,ProcessFeedback"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
