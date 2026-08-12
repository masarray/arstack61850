#!/usr/bin/env python3
"""Prove P0 model fidelity plus the P1 server-authoritative live-value store."""

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


def resolve_read_probe(argument: str) -> str:
    path = Path(argument)
    if path.is_file():
        return str(path)
    if path.is_dir():
        names = {"ariec61850_mms_read_probe", "ariec61850_mms_read_probe.exe"}
        matches = sorted(
            candidate
            for candidate in path.rglob("ariec61850_mms_read_probe*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0])
    raise FileNotFoundError(f"MMS read probe not found under {path}")


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
    parser.add_argument("--scl", required=True)
    args = parser.parse_args()
    read_probe = resolve_read_probe(args.read_probe)

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
                "8000",
                "--state-dump",
                str(state_dump),
                "--exit-after-ms",
                "12000",
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
                rcb_present = (
                    "RCB\tMU01LD0\tLLN0$RP$urcb01\t0\tMU01_LD0_URCB01\t"
                    "MU01LD0\tLLN0$dsSV\t7\t20\t1000\t0" in manifest_text
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
                    and rcb_present
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

            # QA undo runs at 8 s and must mutate the same authoritative store.
            undo_deadline = time.monotonic() + 9.0
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

            app.wait(timeout=16)
            states = json.loads(state_dump.read_text(encoding="utf-8"))
            by_item = {state.get("mmsItem"): state for state in states}
            tctr = by_item["TCTR1$MX$Amp$instMag$i"]
            simcfg = by_item["XCBR1$SP$SimCfg$setVal"]

            expected_tctr = {
                "value": "0",
                "quality": "Good",
                "origin": "gui-undo",
                "timestamp": "1970-01-01T00:00:00.003Z",
                "liveRevision": 3,
            }
            expected_simcfg = {
                "value": "true",
                "quality": "Good",
                "origin": "mms-write",
                "timestamp": "1970-01-01T00:00:00.002Z",
                "liveRevision": 2,
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

            print(
                "IEDSIM_P1_UNIFIED_LIVE_VALUE_PASS "
                "gui_to_mms=42 mms_to_qt=SP:true undo=0 "
                "clock_ms=1,2,3 origin=Simulator-QA,mms-write,gui-undo "
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
