#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def creation_flags() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def manifest() -> str:
    return "\n".join(
        [
            "ARSTACK_IED_MODEL\t2\t1",
            "LN\tTESTIEDLD0\tCSWI1",
            "OBJ\tTESTIEDLD0\tCSWI1$ST$Pos$stVal\tDBPOS\tEnumeration\toff",
            "OBJ\tTESTIEDLD0\tCSWI1$ST$Pos$q\tQUALITY\tQuality\tgood",
            "OBJ\tTESTIEDLD0\tCSWI1$ST$Pos$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
            "OBJ\tTESTIEDLD0\tCSWI1$CF$Pos$ctlModel\tENUM\tEnumeration\t4",
            "CTL\tTESTIEDLD0\tCSWI1\tPos\tDPC\t4",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--control-probe", required=True)
    args = parser.parse_args()

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-iedsim-control-") as directory:
        root = Path(directory)
        model = root / "control.model"
        model_text = manifest()
        if "CTL\tTESTIEDLD0\tCSWI1\tPos\tDPC\t4" not in model_text:
            raise RuntimeError("golden fixture lost engineering CDC=DPC control metadata")
        model.write_text(model_text, encoding="utf-8")
        stdout_path = root / "server.stdout.log"
        stderr_path = root / "server.stderr.log"

        with stdout_path.open("w+", encoding="utf-8") as stdout_handle, \
             stderr_path.open("w+", encoding="utf-8") as stderr_handle:
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
                    "8",
                    "--max-active",
                    "8",
                ],
                stdout=stdout_handle,
                stderr=stderr_handle,
                text=True,
                creationflags=creation_flags(),
            )
            try:
                time.sleep(0.35)
                probe = subprocess.run(
                    [
                        args.control_probe,
                        "127.0.0.1",
                        str(port),
                        "--object",
                        "TESTIEDLD0/CSWI1.Pos",
                        "--action",
                        "select-operate",
                        "--value",
                        "on",
                        "--value-kind",
                        "dpc",
                        "--origin",
                        "ARSAS",
                        "--origin-category",
                        "station",
                        "--interlock-check",
                        "on",
                        "--synchro-check",
                        "on",
                        "--termination-timeout-ms",
                        "3000",
                        "--timeout-ms",
                        "5000",
                        "--arm",
                        "IEC61850-LAB-CONTROL",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=15,
                    check=False,
                    creationflags=creation_flags(),
                )
                if probe.returncode != 0:
                    raise RuntimeError(
                        f"control probe failed (exit={probe.returncode}):\n"
                        f"{probe.stdout}\n{probe.stderr}"
                    )
                required = [
                    "ctlModel=sbo-enhanced",
                    "DISCOVERY_EVIDENCE Oper=structure(ctlVal:boolean,origin:structure(orCat:integer,orIdent:octet-string),ctlNum:unsigned,T:utc-time,Test:boolean,Check:bit-string)",
                    "DISCOVERY_EVIDENCE SBOw=structure(ctlVal:boolean,origin:structure(orCat:integer,orIdent:octet-string),ctlNum:unsigned,T:utc-time,Test:boolean,Check:bit-string)",
                    "STATUS_BEFORE 0x0640",
                    "CONTROL_RESULT action=operate completion=positive-termination "
                    "accepted=true termination=true",
                    "NO_RETRY_EVIDENCE controlWrites=2",
                    "STATUS_AFTER 0x0680",
                ]
                missing = [item for item in required if item not in probe.stdout]
                if missing:
                    raise RuntimeError(
                        f"golden DPC control evidence missing {missing!r}:\n{probe.stdout}"
                    )
            finally:
                server.kill()
                server.wait(timeout=8)

        server_stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
        server_stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
        if (
            "kind=command_termination" not in server_stdout
            or "positive=true" not in server_stdout
            or "kind=control_process_feedback" not in server_stdout
            or "value=on" not in server_stdout
        ):
            raise RuntimeError(
                "server did not expose golden command-termination/process-feedback evidence:\n"
                f"{server_stdout}\n{server_stderr}"
            )

        print(
            "IEDSCOUT_DPC_CONTROL_GOLDEN_PASS "
            "sequence=SBOw,Oper,CommandTermination,ProcessFeedback "
            "ctlVal=boolean process=DBPOS2 originCat=integer "
            "check=sync+interlock"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
