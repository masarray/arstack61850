#!/usr/bin/env python3
"""P3 live IEC 61850 control acceptance against the Qt IED Simulator runtime."""

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


def run_control(
    probe: str,
    port: int,
    object_reference: str,
    action: str,
    value: str = "true",
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            probe,
            "127.0.0.1",
            str(port),
            "--object",
            object_reference,
            "--action",
            action,
            "--value",
            value,
            "--value-kind",
            "bool",
            "--origin",
            "IEDSIM-P3",
            "--origin-category",
            "station",
            "--interlock-check",
            "off",
            "--synchro-check",
            "off",
            "--termination-timeout-ms",
            "3000",
            "--timeout-ms",
            "3000",
            "--arm",
            "IEC61850-LAB-CONTROL",
        ],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
        creationflags=creation_flags(),
    )


def run_read(
    probe: str,
    port: int,
    item: str,
    write_option: str | None = None,
    write_value: str | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [
        probe,
        "127.0.0.1",
        str(port),
        "--domain",
        "MU01LD0",
        "--item",
        item,
        "--timeout-ms",
        "3000",
    ]
    if write_option is not None:
        if write_value is None:
            raise ValueError("write_value required")
        command.extend([write_option, write_value])
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=6,
        check=False,
        creationflags=creation_flags(),
    )


def require_success(result: subprocess.CompletedProcess[str], *needles: str) -> None:
    if result.returncode != 0 or any(needle not in result.stdout for needle in needles):
        raise RuntimeError(
            f"command failed exit={result.returncode} needles={needles} "
            f"stdout={result.stdout} stderr={result.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--read-probe", required=True)
    parser.add_argument("--control-probe", required=True)
    parser.add_argument("--scl", required=True)
    args = parser.parse_args()

    app_binary = resolve_tool(
        args.app,
        {"arstack_ied_simulator", "arstack_ied_simulator.exe"},
        "IED Simulator",
    )
    read_probe = resolve_tool(
        args.read_probe,
        {"ariec61850_mms_read_probe", "ariec61850_mms_read_probe.exe"},
        "MMS read probe",
    )
    control_probe = resolve_tool(
        args.control_probe,
        {"ariec61850_mms_control_interop_client", "ariec61850_mms_control_interop_client.exe"},
        "control interop probe",
    )

    port = free_port()
    environment = dict(os.environ)
    environment["QT_QPA_PLATFORM"] = "offscreen"

    with tempfile.TemporaryDirectory() as temp_dir, tempfile.TemporaryFile(
        mode="w+t", encoding="utf-8"
    ) as app_log:
        state_dump = Path(temp_dir) / "p3-live-state.json"
        app = subprocess.Popen(
            [
                app_binary,
                "--scl",
                args.scl,
                "--port",
                str(port),
                "--runtime",
                "--state-dump",
                str(state_dump),
                "--exit-after-ms",
                "22000",
            ],
            stdout=app_log,
            stderr=subprocess.STDOUT,
            text=True,
            env=environment,
            creationflags=creation_flags(),
        )

        manifest_path = Path(tempfile.gettempdir()) / f"arstack-ied-simulator-{app.pid}.model"
        try:
            deadline = time.monotonic() + 9.0
            manifest = ""
            while time.monotonic() < deadline:
                if app.poll() is not None:
                    raise RuntimeError(f"application exited early: {app.returncode}")
                try:
                    manifest = manifest_path.read_text(encoding="utf-8")
                except (FileNotFoundError, PermissionError, UnicodeDecodeError):
                    manifest = ""
                required = [
                    "CTRL\tMU01LD0\tGGIO1\tSPCSO1\t1\tMU01LD0\tGGIO1$ST$SPCSO1$stVal",
                    "CTRL\tMU01LD0\tGGIO1\tSPCSO2\t2\tMU01LD0\tGGIO1$ST$SPCSO2$stVal",
                    "CTRL\tMU01LD0\tGGIO1\tSPCSO3\t3\tMU01LD0\tGGIO1$ST$SPCSO3$stVal",
                    "CTRL\tMU01LD0\tGGIO1\tSPCSO4\t4\tMU01LD0\tGGIO1$ST$SPCSO4$stVal",
                ]
                if all(entry in manifest for entry in required):
                    break
                time.sleep(0.10)
            else:
                raise RuntimeError(f"P3 CTRL manifest matrix not ready: {manifest}")

            # CF metadata is configuration, never a generic P1 write channel.
            protected_cf = run_read(
                read_probe,
                port,
                "GGIO1$CF$SPCSO1$ctlModel",
                "--write-int",
                "4",
            )
            if protected_cf.returncode == 0:
                raise RuntimeError("ctlModel unexpectedly accepted generic MMS Write")

            # A plain BOOLEAN Write to Oper must be rejected by the strict CO shape.
            malformed_oper = run_read(
                read_probe,
                port,
                "GGIO1$CO$SPCSO1$Oper",
                "--write-bool",
                "true",
            )
            if malformed_oper.returncode == 0:
                raise RuntimeError("CO Oper unexpectedly accepted scalar generic Write")

            direct_normal = run_control(
                control_probe, port, "MU01LD0/GGIO1.SPCSO1", "operate"
            )
            require_success(
                direct_normal,
                "ctlModel=direct-normal",
                "completion=accepted",
                "accepted=true",
                "STATUS_AFTER true",
            )

            # SBO normal: prove Cancel releases without process mutation first.
            sbo_cancel = run_control(
                control_probe, port, "MU01LD0/GGIO1.SPCSO2", "select-cancel"
            )
            require_success(sbo_cancel, "ctlModel=sbo-normal", "STATUS_AFTER false")
            sbo_normal = run_control(
                control_probe, port, "MU01LD0/GGIO1.SPCSO2", "select-operate"
            )
            require_success(
                sbo_normal,
                "ctlModel=sbo-normal",
                "completion=accepted",
                "accepted=true",
                "STATUS_AFTER true",
            )

            direct_enhanced = run_control(
                control_probe, port, "MU01LD0/GGIO1.SPCSO3", "operate"
            )
            require_success(
                direct_enhanced,
                "ctlModel=direct-enhanced",
                "completion=positive-termination",
                "termination=true",
                "STATUS_AFTER true",
            )

            sbo_enhanced = run_control(
                control_probe, port, "MU01LD0/GGIO1.SPCSO4", "select-operate"
            )
            require_success(
                sbo_enhanced,
                "ctlModel=sbo-enhanced",
                "completion=positive-termination",
                "termination=true",
                "STATUS_AFTER true",
            )

            # The same status values are now ordinary authoritative MMS reads.
            for index in range(1, 5):
                readback = run_read(read_probe, port, f"GGIO1$ST$SPCSO{index}$stVal")
                require_success(readback, "value=true")

            app.wait(timeout=28)
            states = json.loads(state_dump.read_text(encoding="utf-8"))
            by_item = {state.get("mmsItem"): state for state in states}
            for index in range(1, 5):
                item = f"GGIO1$ST$SPCSO{index}$stVal"
                state = by_item.get(item)
                if state is None:
                    raise RuntimeError(f"Qt mirror missing {item}")
                if state.get("value") != "true" or state.get("origin") != "mms-control":
                    raise RuntimeError(f"Qt mirror mismatch {item}: {state}")
                if state.get("liveRevision") != index:
                    raise RuntimeError(
                        f"authoritative live revision mismatch {item}: {state.get('liveRevision')} != {index}"
                    )

            app_log.flush()
            app_log.seek(0)
            runtime_log = app_log.read()
            for needle in [
                "kind=control_ready objects=4",
                "kind=control_select",
                "kind=control_operate",
                "kind=control_termination",
                "object=MU01LD0/GGIO1.SPCSO3",
                "object=MU01LD0/GGIO1.SPCSO4",
            ]:
                if needle not in runtime_log:
                    raise RuntimeError(f"missing P3 runtime evidence {needle}: {runtime_log}")
            if runtime_log.count("kind=control_termination") < 2:
                raise RuntimeError(f"enhanced termination count mismatch: {runtime_log}")

            print(
                "IEDSIM_P3_CONTROL_PASS "
                "models=1,2,3,4 direct_normal=true sbo_normal=true "
                "direct_enhanced=true sbo_enhanced=true cancel=true "
                "termination=2 authoritative_store=true qt_mirror=true "
                "generic_write_isolated=true guarded_core=reused"
            )
            return 0
        except BaseException as exception:
            if app.poll() is None:
                app.kill()
                app.wait(timeout=5)
            app_log.seek(0)
            output = app_log.read().strip()
            raise RuntimeError(f"P3 live control test failed: {exception}; app_output={output}") from exception
        finally:
            try:
                manifest_path.unlink(missing_ok=True)
            except OSError:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
