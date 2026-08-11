#!/usr/bin/env python3
"""Loopback smoke for the desktop C#-simulator-parity MMS server."""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import time


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def creation_flags() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--client", required=True)
    args = parser.parse_args()

    port = free_port()
    server = subprocess.Popen(
        [
            args.server,
            "--port",
            str(port),
            "--digital-input-mask",
            "0x35",
            "--max-connections",
            "1",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=creation_flags(),
    )
    try:
        time.sleep(0.4)
        client = subprocess.run(
            [
                args.client,
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

    if client.returncode != 0 or server.returncode != 0:
        raise RuntimeError(
            f"parity loopback failed: client={client.returncode} server={server.returncode}\n"
            f"client stdout:\n{client.stdout}\nclient stderr:\n{client.stderr}\n"
            f"server stdout:\n{server_stdout}\nserver stderr:\n{server_stderr}"
        )

    document = json.loads(client.stdout)
    coverage = document.get("coverage", {})
    expected = {
        "logicalDeviceCount": 1,
        "dataObjectCount": 8,
        "dataAttributeCount": 8,
        "dataSetCount": 1,
    }
    mismatches = {
        key: (coverage.get(key), value)
        for key, value in expected.items()
        if coverage.get(key) != value
    }
    if mismatches or document.get("warnings"):
        raise RuntimeError(
            f"parity discovery mismatch={mismatches} warnings={document.get('warnings')}\n"
            f"client stdout:\n{client.stdout}"
        )
    if "CONNECTION_ACCEPTED" not in server_stdout or "CONNECTION_CLOSED" not in server_stdout:
        raise RuntimeError(f"server lifecycle evidence missing:\n{server_stdout}")

    print("IEDSCOUT_PARITY_LOOPBACK_PASS domain=ESP32S3IOLD0 dataset=EventData")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
