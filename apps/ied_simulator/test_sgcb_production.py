#!/usr/bin/env python3
import argparse
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def find_binary(build_dir: Path, name: str) -> Path:
    candidates = [build_dir / name, build_dir / f"{name}.exe"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    for suffix in (name, f"{name}.exe"):
        matches = list(build_dir.rglob(suffix))
        if matches:
            return matches[0]
    raise RuntimeError(f"Could not find {name} below {build_dir}")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def run_probe(probe: Path, port: int, extra: list[str]) -> str:
    command = [
        str(probe), "127.0.0.1", str(port),
        "--domain", "SGIEDLD0", "--root", "LLN0$SP$SGCB",
        "--expect-num", "4", *extra,
    ]
    result = subprocess.run(command, check=False, text=True, capture_output=True, timeout=15)
    output = result.stdout + result.stderr
    print(output, end="")
    if result.returncode != 0:
        raise RuntimeError(f"SGCB probe failed ({result.returncode}): {output}")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    server = args.server.resolve()
    probe = find_binary(args.build_dir.resolve(), "ariec61850_mms_sgcb_probe")
    if not server.is_file():
        raise RuntimeError(f"Production simulator server does not exist: {server}")

    with tempfile.TemporaryDirectory(prefix="arstack-sgcb-") as temp:
        root = Path(temp)
        manifest = root / "sgcb-production.manifest"
        log_path = root / "server.log"
        manifest.write_text(
            "ARSTACK_IED_MODEL\t2\t1\n"
            "LN\tSGIEDLD0\tLLN0\n"
            "OBJ\tSGIEDLD0\tLLN0$ST$Mod$stVal\tBOOLEAN\tBOOLEAN\tfalse\n"
            "SGCB\tSGIEDLD0\tLLN0$SP$SGCB\t4\t1\n",
            encoding="utf-8",
        )
        port = free_port()
        with log_path.open("w+", encoding="utf-8") as log:
            process = subprocess.Popen(
                [
                    str(server), "--host", "127.0.0.1", "--port", str(port),
                    "--model-manifest", str(manifest), "--max-connections", "2",
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
            )
            try:
                deadline = time.monotonic() + 10.0
                ready = False
                while time.monotonic() < deadline:
                    log.flush()
                    text = log_path.read_text(encoding="utf-8")
                    if "IEDSIM_EVENT kind=server_ready" in text:
                        ready = True
                        break
                    if process.poll() is not None:
                        break
                    time.sleep(0.025)
                if not ready:
                    raise RuntimeError(
                        "Production SGCB server never reached ready state:\n" +
                        log_path.read_text(encoding="utf-8"))
                text = log_path.read_text(encoding="utf-8")
                if "sgcbs=1" not in text:
                    raise RuntimeError("server_ready did not advertise sgcbs=1:\n" + text)

                first = run_probe(
                    probe, port, ["--expect-act", "1", "--activate", "3"])
                if "SGCB_PROBE_PASS discovery=5/5 num=4 initial=1 final=3" not in first or \
                   "activation=verified" not in first:
                    raise RuntimeError("First association evidence is incomplete.")

                second = run_probe(
                    probe, port, ["--expect-act", "3", "--reject", "5"])
                if "SGCB_PROBE_PASS discovery=5/5 num=4 initial=3 final=3" not in second or \
                   "rejection=verified" not in second:
                    raise RuntimeError("Second association evidence is incomplete.")

                process.wait(timeout=10)
                if process.returncode != 0:
                    raise RuntimeError(
                        f"Production SGCB server exited {process.returncode}:\n" +
                        log_path.read_text(encoding="utf-8"))
                text = log_path.read_text(encoding="utf-8")
                if "IEDSIM_EVENT kind=client_error" in text:
                    raise RuntimeError("Production SGCB run emitted client_error:\n" + text)
                if text.count("IEDSIM_EVENT kind=client_connected") != 2:
                    raise RuntimeError("Expected exactly two independent MMS associations.\n" + text)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)

    print(
        "SGCB_PRODUCTION_SOCKET_PASS discovery=5/5 activation=1->3 "
        "cross_association=3 reject_out_of_range=pass associations=2"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
