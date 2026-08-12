#!/usr/bin/env python3
"""Prove P0 model, P1 unified live values, and P2 live reporting."""

from __future__ import annotations

import argparse
import json
import os
import re
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


def resolve_tool(argument: str, names: set[str], description: str) -> str:
    path = Path(argument)
    if path.is_file():
        return str(path)
    if path.is_dir():
        matches = sorted(
            candidate
            for candidate in path.rglob("*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0])
    raise FileNotFoundError(f"{description} not found under {path}")


def resolve_read_probe(argument: str) -> str:
    return resolve_tool(
        argument,
        {"ariec61850_mms_read_probe", "ariec61850_mms_read_probe.exe"},
        "MMS read probe",
    )


def run_probe(
    read_probe: str,
    port: int,
    item: str,
    *,
    type_only: bool = False,
    write_option: str | None = None,
    write_value: str | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [
        read_probe,
        "127.0.0.1",
        str(port),
        "--domain",
        "MU01LD0",
        "--item",
        item,
        "--timeout-ms",
        "3000",
    ]
    if type_only:
        command.append("--type")
    if write_option is not None:
        if write_value is None:
            raise ValueError("write_value is required with write_option")
        command.extend([write_option, write_value])
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=6,
        check=False,
        creationflags=creation_flags(),
    )


def require_probe(
    probe: subprocess.CompletedProcess[str], expected: str, description: str
) -> None:
    if probe.returncode != 0 or expected not in probe.stdout:
        raise RuntimeError(
            f"{description} failed: exit={probe.returncode} "
            f"stdout={probe.stdout} stderr={probe.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--read-probe", required=True)
    parser.add_argument("--report-probe", required=True)
    parser.add_argument("--scl", required=True)
    args = parser.parse_args()
    read_probe = resolve_read_probe(args.read_probe)
    report_probe = resolve_tool(
        args.report_probe,
        {"ariec61850_static_rcb_trial", "ariec61850_static_rcb_trial.exe"},
        "static RCB trial",
    )

    port = free_port()
    environment = dict(os.environ)
    environment["QT_QPA_PLATFORM"] = "offscreen"
    with tempfile.TemporaryDirectory() as temp_dir, tempfile.TemporaryFile(
        mode="w+t", encoding="utf-8"
    ) as app_log:
        state_dump = Path(temp_dir) / "live-state.json"
        app = subprocess.Popen(
            [
                args.app,
                "--scl",
                args.scl,
                "--port",
                str(port),
                "--runtime",
                "--set-first-value",
                "42",
                "--undo-after-ms",
                "15000",
                "--state-dump",
                str(state_dump),
                "--exit-after-ms",
                "24000",
            ],
            stdout=app_log,
            stderr=subprocess.STDOUT,
            text=True,
            env=environment,
            creationflags=creation_flags(),
        )
        last_error = "server not ready"
        manifest_text = ""
        try:
            manifest_path = (
                Path(tempfile.gettempdir()) / f"arstack-ied-simulator-{app.pid}.model"
            )
            manifest_deadline = time.monotonic() + 8.0
            while time.monotonic() < manifest_deadline:
                if app.poll() is not None:
                    raise RuntimeError(
                        f"application exited early with code {app.returncode}"
                    )
                try:
                    manifest_text = manifest_path.read_text(encoding="utf-8")
                except (FileNotFoundError, PermissionError, UnicodeDecodeError):
                    manifest_text = ""

                p0_value_present = (
                    "TCTR1$MX$Amp$instMag$i\tINT16\tNumber\tinteger:16\t0"
                    in manifest_text
                )
                ordered_range_present = (
                    "TCTR1$MX$Amp$instMag$range\tINT8U\tNumber\tunsigned-integer:8\t0"
                    in manifest_text
                )
                structural_only_leaf_present = (
                    "XCBR1$ST$Health$stVal\tBOOLEAN\tBoolean\tboolean\tfalse"
                    in manifest_text
                )
                writable_sp_present = (
                    "XCBR1$SP$SimCfg$setVal\tBOOLEAN\tBoolean\tboolean\tfalse"
                    in manifest_text
                    and "MUT\tMU01LD0\tXCBR1$SP$SimCfg$setVal" in manifest_text
                )
                data_set_present = (
                    "DS\tMU01LD0\tLLN0$dsSV\tMU01LD0\tTCTR1$MX$Amp$instMag$i"
                    in manifest_text
                )
                buffered_data_set_present = (
                    "DS\tMU01LD0\tLLN0$dsBuffered\tMU01LD0\tXCBR1$SP$SimCfg$setVal"
                    in manifest_text
                )
                rcb_present = (
                    "RCB\tMU01LD0\tLLN0$RP$urcb01\t0\tMU01_LD0_URCB01\t"
                    "MU01LD0\tLLN0$dsSV\t7\t20\t1000\t0" in manifest_text
                )
                brcb_present = (
                    "RCB\tMU01LD0\tLLN0$BR$brcb01\t1\tMU01_LD0_BRCB01\t"
                    "MU01LD0\tLLN0$dsBuffered\t8\t20\t0\t0" in manifest_text
                )
                no_ln_scaffold = "\nLN\t" not in "\n" + manifest_text
                order_preserved = (
                    "TCTR1$MX$Amp$instMag$i" in manifest_text
                    and "TCTR1$MX$Amp$instMag$range" in manifest_text
                    and manifest_text.index("TCTR1$MX$Amp$instMag$i")
                    < manifest_text.index("TCTR1$MX$Amp$instMag$range")
                )
                if (
                    manifest_text.startswith("ARSTACK_IED_MODEL\t4\t1\n")
                    and p0_value_present
                    and ordered_range_present
                    and structural_only_leaf_present
                    and writable_sp_present
                    and data_set_present
                    and buffered_data_set_present
                    and rcb_present
                    and brcb_present
                    and no_ln_scaffold
                    and order_preserved
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError(
                    "P0 regression: exact v4 structural model was not published"
                )

            # GUI -> authoritative server store -> MMS Read. The structural manifest
            # must remain revision 1 and keep its initial value, proving the live state
            # is no longer file-reload driven.
            deadline = time.monotonic() + 10.0
            edited_probe: subprocess.CompletedProcess[str] | None = None
            while time.monotonic() < deadline:
                if app.poll() is not None:
                    raise RuntimeError(
                        f"application exited early with code {app.returncode}"
                    )
                edited_probe = run_probe(
                    read_probe, port, "TCTR1$MX$Amp$instMag$i"
                )
                if edited_probe.returncode == 0 and "value=42" in edited_probe.stdout:
                    break
                time.sleep(0.15)
            else:
                assert edited_probe is not None
                raise RuntimeError(
                    "GUI -> server live mutation not visible: "
                    f"{edited_probe.stdout} {edited_probe.stderr}"
                )

            manifest_after_gui = manifest_path.read_text(encoding="utf-8")
            if not manifest_after_gui.startswith("ARSTACK_IED_MODEL\t4\t1\n") or (
                "TCTR1$MX$Amp$instMag$i\tINT16\tNumber\tinteger:16\t0"
                not in manifest_after_gui
            ):
                raise RuntimeError(
                    "GUI mutation unexpectedly rewrote the structural manifest"
                )

            report_trial = subprocess.run(
                [
                    report_probe,
                    "127.0.0.1",
                    str(port),
                    "--preferred-rcb",
                    "MU01LD0/LLN0.urcb01",
                    "--probe-cycles",
                    "6",
                    "--probe-delay-ms",
                    "250",
                    "--timeout-ms",
                    "3000",
                    "--arm",
                    "IEC61850-LAB-STATIC-RCB",
                ],
                capture_output=True,
                text=True,
                timeout=12,
                check=False,
                creationflags=creation_flags(),
            )
            if report_trial.returncode != 0 or "SMART_STATIC_RCB_TRIAL_PASS" not in report_trial.stdout:
                raise RuntimeError(
                    "P2 static reporting trial failed: "
                    f"exit={report_trial.returncode} stdout={report_trial.stdout} stderr={report_trial.stderr}"
                )
            if "STATIC_PLAN selectedRcb=MU01LD0/LLN0.urcb01 mode=URCB dataSet=MU01LD0/LLN0$dsSV" not in report_trial.stdout:
                raise RuntimeError(f"P2 static RCB/DataSet binding mismatch: {report_trial.stdout}")
            evidence = re.search(r"REPORT_EVIDENCE received=(\d+) decodeFailures=(\d+)", report_trial.stdout)
            if evidence is None or int(evidence.group(1)) < 2 or int(evidence.group(2)) != 0:
                raise RuntimeError(f"P2 GI/integrity report evidence missing: {report_trial.stdout}")
            if "REPORT_VALUE" not in report_trial.stdout or "value=42" not in report_trial.stdout:
                raise RuntimeError(f"P2 report did not carry P1 live value 42: {report_trial.stdout}")

            exact_type_probe = run_probe(
                read_probe, port, "TCTR1$MX$Amp$instMag$i", type_only=True
            )
            require_probe(
                exact_type_probe, "kind=integer size=16", "GVAA INT16 evidence"
            )
            exact_unsigned_probe = run_probe(
                read_probe, port, "TCTR1$MX$Amp$instMag$range", type_only=True
            )
            require_probe(
                exact_unsigned_probe,
                "kind=unsigned size=8",
                "GVAA INT8U evidence",
            )

            # Real reverse direction: MMS Write -> authoritative server store -> Qt.
            # Only SCL leaves explicitly marked mmsWritable get a write callback.
            writable_write = run_probe(
                read_probe,
                port,
                "XCBR1$SP$SimCfg$setVal",
                write_option="--write-bool",
                write_value="true",
            )
            require_probe(writable_write, "MMS_WRITE", "bounded SP MMS Write")
            writable_read = run_probe(
                read_probe, port, "XCBR1$SP$SimCfg$setVal"
            )
            require_probe(writable_read, "value=true", "SP write readback")


            brcb_trial = subprocess.run(
                [
                    report_probe,
                    "127.0.0.1",
                    str(port),
                    "--preferred-rcb",
                    "MU01LD0/LLN0.brcb01",
                    "--no-urcb-fallback",
                    "--no-gi",
                    "--exercise-write-bool",
                    "MU01LD0/XCBR1$SP$SimCfg$setVal=false",
                    "--probe-cycles",
                    "5",
                    "--probe-delay-ms",
                    "200",
                    "--timeout-ms",
                    "3000",
                    "--arm",
                    "IEC61850-LAB-STATIC-RCB",
                ],
                capture_output=True,
                text=True,
                timeout=12,
                check=False,
                creationflags=creation_flags(),
            )
            if brcb_trial.returncode != 0 or "SMART_STATIC_RCB_TRIAL_PASS" not in brcb_trial.stdout:
                raise RuntimeError(
                    "P2 BRCB R1-R3 trial failed: "
                    f"exit={brcb_trial.returncode} stdout={brcb_trial.stdout} stderr={brcb_trial.stderr}"
                )
            if "STATIC_PLAN selectedRcb=MU01LD0/LLN0.brcb01 mode=BRCB dataSet=MU01LD0/LLN0$dsBuffered" not in brcb_trial.stdout:
                raise RuntimeError(f"P2 BRCB/DataSet binding mismatch: {brcb_trial.stdout}")
            if "EXERCISE_MMS_WRITE reference=MU01LD0/XCBR1$SP$SimCfg$setVal value=false" not in brcb_trial.stdout:
                raise RuntimeError(f"P2 BRCB live mutation was not executed: {brcb_trial.stdout}")
            brcb_evidence = re.search(r"REPORT_EVIDENCE received=(\d+) decodeFailures=(\d+)", brcb_trial.stdout)
            if brcb_evidence is None or int(brcb_evidence.group(1)) < 1 or int(brcb_evidence.group(2)) != 0:
                raise RuntimeError(f"P2 BRCB InformationReport evidence missing: {brcb_trial.stdout}")
            if "REPORT_VALUE" not in brcb_trial.stdout or "value=false" not in brcb_trial.stdout:
                raise RuntimeError(f"P2 BRCB report did not carry authoritative live value false: {brcb_trial.stdout}")

            # MX is deliberately not generic-write enabled. CO/control remains a P3
            # concern and is not opened by this P1 mutation channel.
            non_writable_write = run_probe(
                read_probe,
                port,
                "TCTR1$MX$Amp$instMag$i",
                write_option="--write-int",
                write_value="99",
            )
            if non_writable_write.returncode == 0:
                raise RuntimeError("MX leaf unexpectedly accepted generic MMS Write")
            still_edited = run_probe(read_probe, port, "TCTR1$MX$Amp$instMag$i")
            require_probe(still_edited, "value=42", "write-boundary preservation")

            structural_probe = run_probe(
                read_probe, port, "XCBR1$ST$Health$stVal"
            )
            require_probe(
                structural_probe, "value=false", "structural-only leaf read"
            )
            rcb_probe = run_probe(read_probe, port, "LLN0$RP$urcb01$RptID")
            require_probe(
                rcb_probe, "value=MU01_LD0_URCB01", "URCB RptID read"
            )

            # QA undo fires after both report trials and must mutate the same authoritative store.
            undo_deadline = time.monotonic() + 18.0
            restored: subprocess.CompletedProcess[str] | None = None
            while time.monotonic() < undo_deadline:
                restored = run_probe(read_probe, port, "TCTR1$MX$Amp$instMag$i")
                if restored.returncode == 0 and "value=0" in restored.stdout:
                    break
                time.sleep(0.15)
            else:
                assert restored is not None
                raise RuntimeError(
                    "Undo did not restore authoritative value: "
                    f"{restored.stdout} {restored.stderr}"
                )

            app.wait(timeout=28)
            states = json.loads(state_dump.read_text(encoding="utf-8"))
            by_item = {state.get("mmsItem"): state for state in states}
            tctr = by_item["TCTR1$MX$Amp$instMag$i"]
            simcfg = by_item["XCBR1$SP$SimCfg$setVal"]

            expected_tctr = {
                "value": "0",
                "quality": "Good",
                "origin": "gui-undo",
                "timestamp": "1970-01-01T00:00:00.004Z",
                "liveRevision": 4,
            }
            expected_simcfg = {
                "value": "false",
                "quality": "Good",
                "origin": "mms-write",
                "timestamp": "1970-01-01T00:00:00.003Z",
                "liveRevision": 3,
            }
            for key, expected in expected_tctr.items():
                if tctr.get(key) != expected:
                    raise RuntimeError(
                        f"Qt mirror TCTR {key}: {tctr.get(key)!r} != {expected!r}"
                    )
            for key, expected in expected_simcfg.items():
                if simcfg.get(key) != expected:
                    raise RuntimeError(
                        f"Qt mirror SimCfg {key}: {simcfg.get(key)!r} != {expected!r}"
                    )

            if not manifest_after_gui.startswith("ARSTACK_IED_MODEL\t4\t1\n"):
                raise RuntimeError("Live state changed structural manifest revision")

            app_log.flush()
            app_log.seek(0)
            runtime_log = app_log.read()
            if "kind=report_sent" not in runtime_log or "reason=gi" not in runtime_log or "reason=integrity" not in runtime_log:
                raise RuntimeError(f"P2 URCB server report-send evidence missing: {runtime_log}")
            if "kind=brcb_event" not in runtime_log or "kind=brcb_captured" not in runtime_log or "kind=brcb_report_sent" not in runtime_log:
                raise RuntimeError(f"P2 BRCB retained-delivery evidence missing: {runtime_log}")
            print(
                "IEDSIM_P2_REPORTING_PASS rptena=true gi=report integrity=report "
                "information_report=true live_value=42 urcb_core=reused "
                "brcb_r1_r3=true retained_capture=true two_phase_commit=true brcb_live_value=false "
                "IEDSIM_P1_UNIFIED_LIVE_VALUE_PASS "
                "gui_to_mms=42 mms_to_qt=SP:true undo=0 "
                "clock_ms=1,2,3,4 origin=Simulator-QA,mms-write,mms-write,gui-undo "
                "generic_write_bound=SP-only"
            )
            return 0
        except BaseException as exception:
            last_error = str(exception) or last_error
            if app.poll() is None:
                app.kill()
                app.wait(timeout=5)
            app_log.seek(0)
            output = app_log.read().strip()
            raise RuntimeError(
                f"GUI P1 live-value test failed: {last_error}; app_output={output}"
            ) from exception


if __name__ == "__main__":
    raise SystemExit(main())
