#!/usr/bin/env python3
"""Server-level static RCB commissioning parity with the accepted IEDScout capture."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


DOMAIN = "AA1E1F06R4Application"


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def creation_flags() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def manifest() -> str:
    lines = [
        "ARSTACK_IED_MODEL\t2\t1",
        f"LN\t{DOMAIN}\tLLN0",
        f"LN\t{DOMAIN}\tGGIO1",
        f"LN\t{DOMAIN}\tMMXU1",
    ]
    for index in range(1, 37):
        item = f"GGIO1$ST$Ind{index:02d}$stVal"
        lines.append(
            f"OBJ\t{DOMAIN}\t{item}\tBOOLEAN\tBoolean\tfalse"
        )
        lines.append(
            f"DS\t{DOMAIN}\tLLN0$Digital\t{DOMAIN}\t{item}"
        )
    for index in range(1, 23):
        item = f"MMXU1$MX$An{index:02d}$mag"
        lines.append(
            f"OBJ\t{DOMAIN}\t{item}\tINT32\tNumber\t{index}"
        )
        lines.append(
            f"DS\t{DOMAIN}\tLLN0$Analog\t{DOMAIN}\t{item}"
        )

    # Static commissioning values reproduce the accepted IEDScout session.
    # No OptFlds rewrite is made by the client probe.
    lines.append(
        "RCB\t" + DOMAIN + "\tLLN0$RP$Unbuffer01\t0\t"
        "AA1E1F06R4/Application/LLN0$RP$Unbuffer\t"
        + DOMAIN + "\tLLN0$Analog\t100001\t0\t0\t124\t120\t128"
    )
    lines.append(
        "RCB\t" + DOMAIN + "\tLLN0$BR$Buffer01\t1\t"
        "AA1E1F06R4/Application/LLN0$BR$Buffer\t"
        + DOMAIN + "\tLLN0$Digital\t100001\t100\t0\t124\t121\t128"
    )
    lines.append("")
    return "\n".join(lines)


def run_probe(executable: str, port: int, mode: str) -> subprocess.CompletedProcess[str]:
    if mode == "urcb":
        rcb = "LLN0$RP$Unbuffer01"
        rptid = "AA1E1F06R4/Application/LLN0$RP$Unbuffer"
        dataset = f"{DOMAIN}/LLN0$Analog"
        members = "22"
        opt_first = "0x78"
    else:
        rcb = "LLN0$BR$Buffer01"
        rptid = "AA1E1F06R4/Application/LLN0$BR$Buffer"
        dataset = f"{DOMAIN}/LLN0$Digital"
        members = "36"
        opt_first = "0x79"

    return subprocess.run(
        [
            executable,
            "127.0.0.1",
            str(port),
            "--mode",
            mode,
            "--domain",
            DOMAIN,
            "--rcb",
            rcb,
            "--expected-rptid",
            rptid,
            "--expected-dataset",
            dataset,
            "--expected-members",
            members,
            "--expected-confrev",
            "100001",
            "--expected-opt-first",
            opt_first,
            "--expected-opt-second",
            "0x80",
            "--timeout-ms",
            "5000",
        ],
        capture_output=True,
        text=True,
        timeout=12,
        check=False,
        creationflags=creation_flags(),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--wire-probe", required=True)
    args = parser.parse_args()

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-reporting-wire-") as directory:
        root = Path(directory)
        model = root / "reporting.model"
        model.write_text(manifest(), encoding="utf-8", newline="\n")
        stdout_path = root / "server.stdout.log"
        stderr_path = root / "server.stderr.log"

        with stdout_path.open("w+", encoding="utf-8") as out, \
             stderr_path.open("w+", encoding="utf-8") as err:
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
                    "4",
                    "--max-active",
                    "4",
                ],
                stdout=out,
                stderr=err,
                text=True,
                creationflags=creation_flags(),
            )
            try:
                time.sleep(0.35)
                urcb = run_probe(args.wire_probe, port, "urcb")
                if (
                    urcb.returncode != 0
                    or "IEDSCOUT_REPORTING_WIRE_PASS mode=urcb" not in urcb.stdout
                    or "members=22" not in urcb.stdout
                    or "groupedWrite=TrgOps,RptEna" not in urcb.stdout
                    or "reservation=urcb-only" not in urcb.stdout
                ):
                    raise RuntimeError(
                        "URCB IEDScout commissioning parity failed: "
                        f"exit={urcb.returncode} stdout={urcb.stdout!r} "
                        f"stderr={urcb.stderr!r}"
                    )

                brcb = run_probe(args.wire_probe, port, "brcb")
                if (
                    brcb.returncode != 0
                    or "IEDSCOUT_REPORTING_WIRE_PASS mode=brcb" not in brcb.stdout
                    or "members=36" not in brcb.stdout
                    or "groupedWrite=TrgOps,RptEna" not in brcb.stdout
                    or "reservation=none" not in brcb.stdout
                ):
                    raise RuntimeError(
                        "BRCB IEDScout commissioning parity failed: "
                        f"exit={brcb.returncode} stdout={brcb.stdout!r} "
                        f"stderr={brcb.stderr!r}"
                    )
            finally:
                server.kill()
                server.wait(timeout=8)

        server_stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
        server_stderr = stderr_path.read_text(encoding="utf-8", errors="replace")

    if "kind=server_ready" not in server_stdout:
        raise RuntimeError(
            "server readiness evidence missing:\n"
            + server_stdout + "\n" + server_stderr
        )
    if "kind=client_error" in server_stdout or "protocol_error" in server_stdout:
        raise RuntimeError(
            "server reported client/protocol error:\n"
            + server_stdout + "\n" + server_stderr
        )

    print(
        "IEDSCOUT_REPORTING_COMMISSIONING_PASS "
        "urcb=Resv,grouped-TrgOps-RptEna,GI,report "
        "brcb=grouped-TrgOps-RptEna,GI,report "
        "urcbMembers=22 brcbMembers=36"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
