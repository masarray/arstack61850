#!/usr/bin/env python3
"""Loopback contract between runnable MMS servers and live_discover."""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class Profile:
    name: str
    server: str
    server_args: tuple[str, ...]
    logical_nodes: int
    data_objects: int
    data_attributes: int
    data_sets: int
    report_controls: int
    type_probes: int
    exact_types: int


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def creation_flags() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def run_profile(client: str, profile: Profile) -> None:
    port = free_port()
    command = [
        profile.server,
        "--bind",
        "127.0.0.1",
        "--port",
        str(port),
        *profile.server_args,
        "--once",
    ]
    server = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=creation_flags(),
    )
    client_result: subprocess.CompletedProcess[str] | None = None
    try:
        time.sleep(0.5)
        client_result = subprocess.run(
            [
                client,
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
        )
        server_stdout, server_stderr = server.communicate(timeout=8)
    except BaseException:
        server.kill()
        server_stdout, server_stderr = server.communicate()
        raise

    if client_result.returncode != 0 or server.returncode != 0:
        raise RuntimeError(
            f"{profile.name} loopback failed: client={client_result.returncode}, "
            f"server={server.returncode}\n"
            f"client stdout:\n{client_result.stdout}\n"
            f"client stderr:\n{client_result.stderr}\n"
            f"server stdout:\n{server_stdout}\n"
            f"server stderr:\n{server_stderr}"
        )

    document = json.loads(client_result.stdout)
    coverage = document["coverage"]
    expected = {
        "logicalDeviceCount": 1,
        "logicalNodeCount": profile.logical_nodes,
        "dataObjectCount": profile.data_objects,
        "dataAttributeCount": profile.data_attributes,
        "dataSetCount": profile.data_sets,
        "reportControlCount": profile.report_controls,
        "variableTypeReadAttemptCount": profile.type_probes,
        "variableTypeReadSuccessCount": profile.type_probes,
        "variableTypeReadFailureCount": 0,
        "exactMmsTypeCount": profile.exact_types,
    }
    mismatches = {
        key: (coverage.get(key), value)
        for key, value in expected.items()
        if coverage.get(key) != value
    }
    warnings = document.get("warnings", [])
    if mismatches or warnings:
        raise RuntimeError(
            f"{profile.name} discovery mismatch: {mismatches}; warnings={warnings}"
        )
    if "CLIENT_ACCEPTED" not in server_stdout or "CLIENT_CLOSED" not in server_stdout:
        raise RuntimeError(
            f"{profile.name} server lifecycle evidence missing:\n{server_stdout}"
        )

    print(
        f"{profile.name}: LD=1 LN={profile.logical_nodes} "
        f"DO={profile.data_objects} DA={profile.data_attributes} "
        f"DataSet={profile.data_sets} RCB={profile.report_controls} "
        f"exactTypes={profile.exact_types}"
    )


def run_direct_control(inventory: str, client: str, server_executable: str) -> None:
    port = free_port()
    server = subprocess.Popen(
        [server_executable, "--bind", "127.0.0.1", "--port", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=creation_flags(),
    )
    try:
        time.sleep(0.5)
        inventory_result = subprocess.run(
            [inventory, "127.0.0.1", str(port), "--timeout-ms", "3000"],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        operate_result = subprocess.run(
            [
                client,
                "127.0.0.1",
                str(port),
                "--object",
                "ESP32S3IOLD0/GGIO1.SPCSO1",
                "--action",
                "operate",
                "--value",
                "true",
                "--synchro-check",
                "off",
                "--interlock-check",
                "off",
                "--arm",
                "IEC61850-LAB-CONTROL",
                "--timeout-ms",
                "3000",
            ],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        verify_result = subprocess.run(
            [
                client,
                "127.0.0.1",
                str(port),
                "--object",
                "ESP32S3IOLD0/GGIO1.SPCSO1",
                "--timeout-ms",
                "3000",
            ],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    finally:
        server.terminate()
        try:
            server_stdout, server_stderr = server.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            server.kill()
            server_stdout, server_stderr = server.communicate()

    failures = []
    if inventory_result.returncode != 0 or "candidates=8 ready=8" not in inventory_result.stdout:
        failures.append(
            f"inventory exit={inventory_result.returncode}\n"
            f"stdout:\n{inventory_result.stdout}\nstderr:\n{inventory_result.stderr}"
        )
    if (
        operate_result.returncode != 0
        or "completion=accepted accepted=true" not in operate_result.stdout
        or "STATUS_AFTER true" not in operate_result.stdout
        or "NO_RETRY_EVIDENCE controlWrites=1" not in operate_result.stdout
    ):
        failures.append(
            f"operate exit={operate_result.returncode}\n"
            f"stdout:\n{operate_result.stdout}\nstderr:\n{operate_result.stderr}"
        )
    if verify_result.returncode != 0 or "STATUS_BEFORE true" not in verify_result.stdout:
        failures.append(
            f"verify exit={verify_result.returncode}\n"
            f"stdout:\n{verify_result.stdout}\nstderr:\n{verify_result.stderr}"
        )
    if failures:
        raise RuntimeError(
            "direct control loopback failed:\n"
            + "\n".join(failures)
            + f"\nserver stdout:\n{server_stdout}\nserver stderr:\n{server_stderr}"
        )
    print("direct-control: candidates=8 ready=8 operate=accepted status=true writes=1")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--control-inventory", required=True)
    parser.add_argument("--control-client", required=True)
    parser.add_argument("--direct-server", required=True)
    parser.add_argument("--urcb-server", required=True)
    args = parser.parse_args()

    profiles = (
        Profile(
            "direct",
            args.direct_server,
            ("--di-mask", "0xA5", "--do-mask", "0x3C"),
            3,
            18,
            34,
            1,
            0,
            3,
            34,
        ),
        Profile(
            "urcb",
            args.urcb_server,
            ("--mask", "0x3C"),
            2,
            9,
            19,
            1,
            1,
            2,
            19,
        ),
    )
    for profile in profiles:
        run_profile(args.client, profile)
    run_direct_control(
        args.control_inventory, args.control_client, args.direct_server
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # noqa: BLE001 - evidence must include all failures.
        print(f"MMS server loopback failed: {error}", file=sys.stderr)
        raise SystemExit(1)
