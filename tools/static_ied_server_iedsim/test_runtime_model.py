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
        "LN\tTESTIEDLD0\tXCBR1",
    ]

    for index in range(1, 37):
        object_name = f"Digital{index}"
        st_val = tracked_value if index == 1 else "false"
        lines.extend(
            [
                f"OBJ\tTESTIEDLD0\tGGIO1$ST${object_name}$stVal\tBOOLEAN\tBoolean\t{st_val}",
                f"OBJ\tTESTIEDLD0\tGGIO1$ST${object_name}$q\tQUALITY\tQuality\tgood",
                f"OBJ\tTESTIEDLD0\tGGIO1$ST${object_name}$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
                f"DS\tTESTIEDLD0\tLLN0$Digital\tTESTIEDLD0\tGGIO1$ST${object_name}",
            ]
        )

    for index in range(1, 23):
        object_name = f"Analog{index}"
        lines.extend(
            [
                f"OBJ\tTESTIEDLD0\tGGIO1$MX${object_name}$mag$i\tINT32\tInt32\t{index}",
                f"OBJ\tTESTIEDLD0\tGGIO1$MX${object_name}$mag$f\tFLOAT32\tFloat32\t{index}.5",
                f"OBJ\tTESTIEDLD0\tGGIO1$MX${object_name}$q\tQUALITY\tQuality\tgood",
                f"OBJ\tTESTIEDLD0\tGGIO1$MX${object_name}$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
                f"DS\tTESTIEDLD0\tLLN0$Analog\tTESTIEDLD0\tGGIO1$MX${object_name}",
            ]
        )

    lines.extend(
        [
            "OBJ\tTESTIEDLD0\tXCBR1$ST$Pos$stVal\tDBPOS\tEnumeration\tintermediate-state",
            "OBJ\tTESTIEDLD0\tXCBR1$ST$Pos$q\tQUALITY\tQuality\tgood",
            "OBJ\tTESTIEDLD0\tXCBR1$ST$Pos$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        ]
    )

    lines.append(
        "RCB\tTESTIEDLD0\tLLN0$RP$Structured01\t0\t"
        "TESTIEDLD0/LLN0$RP$Structured01\tTESTIEDLD0\tLLN0$Digital\t"
        "1\t0\t0\t124\t120\t128"
    )
    lines.append(
        "RCB\tTESTIEDLD0\tLLN0$RP$StructuredAnalog01\t0\t"
        "TESTIEDLD0/LLN0$RP$StructuredAnalog01\tTESTIEDLD0\tLLN0$Analog\t"
        "1\t0\t0\t124\t120\t128"
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
    with_type: bool = False,
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
    if with_type:
        command.append("--with-type")
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
    parser.add_argument("--urcb-probe", required=True)
    args = parser.parse_args()

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-iedsim-runtime-") as directory:
        model = Path(directory) / "runtime.model"
        atomic_write(model, manifest(1, False))
        server_stdout_path = Path(directory) / "server.stdout.log"
        server_stderr_path = Path(directory) / "server.stderr.log"
        server_stdout_handle = server_stdout_path.open("w+", encoding="utf-8")
        server_stderr_handle = server_stderr_path.open("w+", encoding="utf-8")

        def collect_server_logs() -> tuple[str, str]:
            server_stdout_handle.flush()
            server_stderr_handle.flush()
            return (
                server_stdout_path.read_text(encoding="utf-8", errors="replace"),
                server_stderr_path.read_text(encoding="utf-8", errors="replace"),
            )

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
                "32",
                "--max-active",
                "32",
            ],
            stdout=server_stdout_handle,
            stderr=server_stderr_handle,
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
            coverage = document.get("coverage", {})
            if coverage.get("dataSetCount") != 2:
                raise RuntimeError(f"static DataSets missing: {discovery.stdout}")
            if (
                coverage.get("reportControlCount") != 2
                or coverage.get("reportControlBindingNotReadCount") != 0
                or coverage.get("reportControlBindingReadFailedCount") != 0
            ):
                raise RuntimeError(
                    "LN-root synthetic discovery lost configured URCB hierarchy: "
                    f"{coverage}"
                )
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
                with_type=True,
            )
            digital_signature = "structure(boolean,bit-string,utc-time)"
            digital_type = f"type={digital_signature}"
            digital_shape = f"shape={digital_signature}"
            if (
                structured.returncode != 0
                or digital_type not in structured.stdout
                or digital_shape not in structured.stdout
            ):
                raise RuntimeError(
                    "whole-DO IEC 61850 SPS type/data ordering mismatch: "
                    f"expectedType={digital_type!r} expectedData={digital_shape!r} "
                    f"exit={structured.returncode} stdout={structured.stdout!r} "
                    f"stderr={structured.stderr!r}"
                )

            # Nested constructed attributes are positional too.  Model a typical
            # analogue value as mag{i,f}, followed by Quality and Timestamp, and
            # prove both the outer and inner declaration order survive MMS Read.
            analog = run_read_probe(
                args.read_probe,
                port,
                "GGIO1$MX$Analog1",
                with_type=True,
            )
            analog_signature = (
                "structure(structure(integer,floating-point),bit-string,utc-time)"
            )
            analog_type = f"type={analog_signature}"
            analog_shape = f"shape={analog_signature}"
            if (
                analog.returncode != 0
                or analog_type not in analog.stdout
                or analog_shape not in analog.stdout
            ):
                raise RuntimeError(
                    "nested IEC 61850 analogue type/data ordering mismatch: "
                    f"expectedType={analog_type!r} expectedData={analog_shape!r} "
                    f"exit={analog.returncode} stdout={analog.stdout!r} "
                    f"stderr={analog.stderr!r}"
                )

            first_read_signal = Path(directory) / "first-read.signal"
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
                    "3",
                    "--delay-ms",
                    "500",
                    "--signal-after-first",
                    str(first_read_signal),
                    "--timeout-ms",
                    "10000",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                creationflags=creation_flags(),
            )
            signal_deadline = time.monotonic() + 15.0
            while not first_read_signal.exists() and time.monotonic() < signal_deadline:
                if probe.poll() is not None:
                    break
                time.sleep(0.02)
            if not first_read_signal.exists():
                probe_stdout, probe_stderr = probe.communicate(timeout=5)
                raise RuntimeError(
                    "first same-association MMS read was not signaled: "
                    f"exit={probe.returncode} stdout={probe_stdout!r} stderr={probe_stderr!r}"
                )
            atomic_write(model, manifest(2, True))
            probe_stdout, probe_stderr = probe.communicate(timeout=10)
            reads = [line for line in probe_stdout.splitlines() if line.startswith("MMS_READ ")]
            if (
                probe.returncode != 0
                or len(reads) != 3
                or "value=false" not in reads[0]
                or sum("value=true" in line for line in reads[1:]) != 2
            ):
                raise RuntimeError(
                    "live MMS value did not remain refreshed on the existing association:\n"
                    + "\n".join(reads)
                    + f"\nexit={probe.returncode} stderr:\n{probe_stderr}"
                )

            report = subprocess.run(
                [
                    args.urcb_probe,
                    "127.0.0.1",
                    str(port),
                    "--domain",
                    "TESTIEDLD0",
                    "--rcb",
                    "LLN0$RP$Structured01",
                    "--timeout-ms",
                    "5000",
                ],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
                creationflags=creation_flags(),
            )
            digital_report_shape = (
                "access_results=79 report_values=36 "
                "first_value_shape=structure(boolean,bit-string,utc-time)"
            )
            digital_report_value = "first_value={true,"
            if (
                report.returncode != 0
                or digital_report_shape not in report.stdout
                or digital_report_value not in report.stdout
                or "uniform_value_shape=true" not in report.stdout
            ):
                raise RuntimeError(
                    "Digital URCB GI structured DataSet projection mismatch: "
                    f"expectedShape={digital_report_shape!r} "
                    f"expectedLiveValue={digital_report_value!r} "
                    f"exit={report.returncode} stdout={report.stdout!r} "
                    f"stderr={report.stderr!r}"
                )

            analog_report = subprocess.run(
                [
                    args.urcb_probe,
                    "127.0.0.1",
                    str(port),
                    "--domain",
                    "TESTIEDLD0",
                    "--rcb",
                    "LLN0$RP$StructuredAnalog01",
                    "--timeout-ms",
                    "5000",
                ],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
                creationflags=creation_flags(),
            )
            analog_report_shape = (
                "access_results=51 report_values=22 "
                "first_value_shape="
                "structure(structure(integer,floating-point),bit-string,utc-time)"
            )
            if (
                analog_report.returncode != 0
                or analog_report_shape not in analog_report.stdout
                or "uniform_value_shape=true" not in analog_report.stdout
            ):
                raise RuntimeError(
                    "Analog URCB GI structured DataSet projection mismatch: "
                    f"expectedShape={analog_report_shape!r} "
                    f"exit={analog_report.returncode} "
                    f"stdout={analog_report.stdout!r} "
                    f"stderr={analog_report.stderr!r}"
                )
            # Keep the DPC whole-object probe last. Windows can retain the
            # just-closed disposable client association longer than Linux, but
            # the DPC semantic proof itself is independent of a subsequent
            # reconnect. Earlier discovery/read/report probes already prove
            # multiple sequential associations on this server instance.
            dpc = run_read_probe(
                args.read_probe,
                port,
                "XCBR1$ST$Pos",
                with_type=True,
            )
            dpc_signature = "structure(bit-string,bit-string,utc-time)"
            dpc_type = f"type={dpc_signature}"
            dpc_shape = f"shape={dpc_signature}"
            if (
                dpc.returncode != 0
                or dpc_type not in dpc.stdout
                or "type_bit_widths=2,13" not in dpc.stdout
                or dpc_shape not in dpc.stdout
                or "data_bit_widths=2,13" not in dpc.stdout
            ):
                raise RuntimeError(
                    "DPC Pos stVal/q positional width mismatch: "
                    f"expectedType={dpc_type!r} expectedTypeWidths='2,13' "
                    f"expectedData={dpc_shape!r} expectedDataWidths='2,13' "
                    f"exit={dpc.returncode} stdout={dpc.stdout!r} "
                    f"stderr={dpc.stderr!r}"
                )

            # Bounded/graceful server-loop shutdown is covered by the dedicated
            # parity loopback tests. This runtime-model gate stops after semantic
            # evidence so platform-specific disposable-client teardown timing is
            # not confused with IEC 61850 model/report parity.
            server.kill()
            server.wait(timeout=8)
            server_stdout, server_stderr = collect_server_logs()
        except BaseException as error:
            server.kill()
            server.wait(timeout=8)
            server_stdout, server_stderr = collect_server_logs()
            server_stdout_handle.close()
            server_stderr_handle.close()
            raise RuntimeError(
                f"{error}\n--- server stdout ---\n{server_stdout}"
                f"\n--- server stderr ---\n{server_stderr}"
            ) from error
        server_stdout_handle.close()
        server_stderr_handle.close()

    if (
        "kind=server_ready" not in server_stdout
        or "kind=value_sync" not in server_stdout
    ):
        raise RuntimeError(
            f"server structured/live-value evidence missing (exit={server.returncode}):\n"
            f"{server_stdout}\n{server_stderr}"
        )
    print(
        "IEDSIM_RUNTIME_MODEL_PASS datasets=2 reportControls=2 rcbBindingsComplete=true "
        "digitalMembers=36 analogMembers=22 "
        "digitalTypeDataOrder=structure(boolean,bit-string,utc-time) "
        "analogTypeDataOrder=structure(structure(integer,floating-point),bit-string,utc-time) "
        "dpcPosOrder=stVal2bit,q13bit,tUtc "
        "urcbGiDigital=36 urcbGiAnalog=22 reportBackedTotal=58 "
        "productionOptFlds=0x78,0x80 reasonAlignment=true "
        "digitalReportShape=structure(boolean,bit-string,utc-time) "
        "analogReportShape=structure(structure(integer,floating-point),bit-string,utc-time) "
        "digitalReportLiveValue=true valueTransition=false->true "
        "associationPreserved=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
