#!/usr/bin/env python3
import argparse
import json
import pathlib
import socket
import subprocess
import tempfile
import time


def free_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def run_client(client: str, port: int, *args: str) -> dict:
    completed = subprocess.run(
        [client, "127.0.0.1", str(port), *args, "--json"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=20,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"client failed rc={completed.returncode} args={args}\n"
            f"stdout={completed.stdout}\nstderr={completed.stderr}"
        )
    lines = [line for line in completed.stdout.splitlines() if line.strip().startswith("{")]
    if not lines:
        raise RuntimeError(f"client produced no JSON: {completed.stdout!r}")
    return json.loads(lines[-1])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--client", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="arstack-file-server-") as directory:
        root = pathlib.Path(directory)
        payload = bytes(((index * 29 + 11) & 0xFF) for index in range(70_000))
        (root / "FRA00028.dat").write_bytes(payload)
        (root / "FRA00028.cfg").write_text("CFG\n", encoding="ascii")
        (root / "nested").mkdir()
        download = root / "download.bin"
        port = free_port()

        server = subprocess.Popen(
            [
                args.server,
                "--host", "127.0.0.1",
                "--port", str(port),
                "--file-root", str(root),
                "--allow-file-delete",
                "--max-connections", "4",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            time.sleep(0.25)
            first = run_client(args.client, port, "list")
            names = {entry["name"] for entry in first.get("entries", [])}
            if not {"FRA00028.cfg", "FRA00028.dat", "nested"}.issubset(names):
                raise RuntimeError(f"initial FileDirectory missing entries: {first}")

            transfer = run_client(
                args.client, port, "download",
                "--remote", "FRA00028.dat",
                "--output", str(download),
            )
            if not transfer.get("success") or not transfer.get("remoteFileClosed"):
                raise RuntimeError(f"download did not close cleanly: {transfer}")
            if download.read_bytes() != payload:
                raise RuntimeError("downloaded bytes differ from source")

            deleted = run_client(
                args.client, port, "delete", "--remote", "FRA00028.cfg"
            )
            if not deleted.get("success"):
                raise RuntimeError(f"FileDelete failed: {deleted}")
            if (root / "FRA00028.cfg").exists():
                raise RuntimeError("FileDelete returned success but file still exists")

            refreshed = run_client(args.client, port, "list")
            refreshed_names = {entry["name"] for entry in refreshed.get("entries", [])}
            if "FRA00028.cfg" in refreshed_names or "FRA00028.dat" not in refreshed_names:
                raise RuntimeError(f"directory refresh is inconsistent: {refreshed}")

            stdout, stderr = server.communicate(timeout=15)
            if server.returncode != 0:
                raise RuntimeError(
                    f"server failed rc={server.returncode}\nstdout={stdout}\nstderr={stderr}"
                )
            required = ["service=FileDirectory", "service=FileOpen", "service=FileRead",
                        "service=FileClose", "service=FileDelete"]
            missing = [marker for marker in required if marker not in stdout]
            if missing:
                raise RuntimeError(
                    f"server diagnostics missing {missing}\nstdout={stdout}\nstderr={stderr}"
                )
            print(
                "IEDSCOUT_FILE_SERVER_LOOPBACK_PASS "
                "directory=pass open=pass read=pass close=pass delete=pass "
                "refresh=pass segmented_payload=70000"
            )
            return 0
        finally:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=3)


if __name__ == "__main__":
    raise SystemExit(main())
