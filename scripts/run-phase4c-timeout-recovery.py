#!/usr/bin/env python3
"""Run controlled read-only Phase 4C timeout/recovery evidence.

The runner proves a healthy baseline, injects a post-association receive fault through
an in-process TCP proxy, requires a real client request timeout, then performs a
fresh direct association and requires the recovered structural model to match the
baseline. The proxy never mutates IEC 61850 payloads and never emits requests of its
own; it only forwards bytes and withholds server TPKT frames after the association
response has completed.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from typing import Any


TIMEOUT_RE = re.compile(r"(?:timeout|timed\s+out|deadline)", re.IGNORECASE)


def find_binary(root: pathlib.Path, override: str | None) -> pathlib.Path:
    if override:
        path = pathlib.Path(override).resolve()
        if not path.is_file():
            raise FileNotFoundError(f"Discovery binary not found: {path}")
        return path

    for name in ("ariec61850_live_discover.exe", "ariec61850_live_discover"):
        for candidate in (root / "build" / "Release" / name, root / "build" / name):
            if candidate.is_file():
                return candidate
    raise FileNotFoundError(
        "Build ariec61850_live_discover first; searched build/Release and build."
    )


def send_all(sock: socket.socket, data: bytes) -> None:
    view = memoryview(data)
    while view:
        sent = sock.send(view)
        if sent <= 0:
            raise ConnectionError("socket send returned zero bytes")
        view = view[sent:]


class TpktStreamGate:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[bytes]:
        self._buffer.extend(data)
        frames: list[bytes] = []
        while len(self._buffer) >= 4:
            if self._buffer[0] != 3 or self._buffer[1] != 0:
                raise RuntimeError("proxy observed a non-TPKT server stream")
            length = int.from_bytes(self._buffer[2:4], "big")
            if length < 4:
                raise RuntimeError("proxy observed an invalid TPKT length")
            if len(self._buffer) < length:
                break
            frames.append(bytes(self._buffer[:length]))
            del self._buffer[:length]
        return frames


def is_cotp_data_eot(tpkt: bytes) -> bool:
    if len(tpkt) < 7:
        return False
    cotp = tpkt[4:]
    if len(cotp) < 3:
        return False
    header_length = cotp[0]
    if header_length < 2 or len(cotp) < header_length + 1:
        return False
    return (cotp[1] & 0xF0) == 0xF0 and (cotp[2] & 0x80) != 0


@dataclass
class ProxyEvidence:
    listen_host: str = "127.0.0.1"
    listen_port: int = 0
    association_response_forwarded: bool = False
    fault_triggered: bool = False
    withheld_tpkt_count: int = 0
    withheld_bytes: int = 0
    client_to_server_bytes: int = 0
    server_to_client_forwarded_bytes: int = 0
    error: str = ""


class PostAssociationStallProxy:
    def __init__(self, target_host: str, target_port: int) -> None:
        self._target_host = target_host
        self._target_port = target_port
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._listener: socket.socket | None = None
        self._client: socket.socket | None = None
        self._upstream: socket.socket | None = None
        self.evidence = ProxyEvidence()

    def start(self) -> None:
        self._thread.start()
        if not self._ready.wait(timeout=5.0):
            raise RuntimeError("timeout waiting for fault-injection proxy to start")
        if self.evidence.error:
            raise RuntimeError(self.evidence.error)

    def stop(self) -> None:
        self._stop.set()
        for sock in (self._client, self._upstream, self._listener):
            if sock is not None:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    sock.close()
                except OSError:
                    pass
        self._thread.join(timeout=5.0)

    def _copy_client_to_server(self) -> None:
        assert self._client is not None
        assert self._upstream is not None
        try:
            while not self._stop.is_set():
                try:
                    data = self._client.recv(64 * 1024)
                except socket.timeout:
                    continue
                except OSError:
                    return
                if not data:
                    return
                self.evidence.client_to_server_bytes += len(data)
                send_all(self._upstream, data)
        except Exception as exc:  # pragma: no cover - evidence is surfaced by caller
            if not self._stop.is_set():
                self.evidence.error = f"client->server proxy failure: {exc}"
                self._stop.set()

    def _run(self) -> None:
        gate = TpktStreamGate()
        try:
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._listener = listener
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((self.evidence.listen_host, 0))
            listener.listen(1)
            listener.settimeout(0.2)
            self.evidence.listen_port = int(listener.getsockname()[1])
            self._ready.set()

            while not self._stop.is_set():
                try:
                    client, _ = listener.accept()
                    self._client = client
                    break
                except socket.timeout:
                    continue
            if self._stop.is_set() or self._client is None:
                return

            self._client.settimeout(0.2)
            upstream = socket.create_connection(
                (self._target_host, self._target_port), timeout=5.0
            )
            self._upstream = upstream
            upstream.settimeout(0.2)

            copier = threading.Thread(target=self._copy_client_to_server, daemon=True)
            copier.start()

            while not self._stop.is_set():
                try:
                    data = upstream.recv(64 * 1024)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not data:
                    break

                for frame in gate.feed(data):
                    if self.evidence.association_response_forwarded:
                        self.evidence.fault_triggered = True
                        self.evidence.withheld_tpkt_count += 1
                        self.evidence.withheld_bytes += len(frame)
                        continue

                    send_all(self._client, frame)
                    self.evidence.server_to_client_forwarded_bytes += len(frame)
                    if is_cotp_data_eot(frame):
                        # The first complete server COTP Data application payload after
                        # COTP CC is the Session/Presentation/ACSE association response.
                        # Forward it in full, then withhold subsequent server TPKTs so
                        # the first confirmed MMS service request experiences a real
                        # receive deadline expiry.
                        self.evidence.association_response_forwarded = True

            self._stop.set()
            copier.join(timeout=1.0)
        except Exception as exc:
            self.evidence.error = str(exc)
            self._ready.set()
        finally:
            for sock in (self._client, self._upstream, self._listener):
                if sock is not None:
                    try:
                        sock.close()
                    except OSError:
                        pass


def run_process(command: list[str], timeout_seconds: float) -> tuple[subprocess.CompletedProcess[str], int]:
    started = time.monotonic()
    try:
        process = subprocess.run(
            command,
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        raise RuntimeError(
            "discovery process exceeded the outer evidence-runner bound; "
            f"stdout={stdout!r} stderr={stderr!r}"
        ) from exc
    elapsed_ms = int((time.monotonic() - started) * 1000)
    return process, elapsed_ms


def model_command(
    binary: pathlib.Path,
    host: str,
    port: int,
    timeout_ms: int,
    max_rcb: int,
) -> list[str]:
    return [
        str(binary),
        host,
        str(port),
        "--model-json",
        "--timeout-ms",
        str(timeout_ms),
        "--no-types",
        "--no-datasets",
        "--max-rcb",
        str(max_rcb),
        "--control-block-values",
        "--max-control-blocks",
        "8",
        "--max-control-block-attributes",
        "32",
    ]


def run_model(
    binary: pathlib.Path,
    host: str,
    port: int,
    timeout_ms: int,
    max_rcb: int,
    output_path: pathlib.Path,
) -> dict[str, Any]:
    command = model_command(binary, host, port, timeout_ms, max_rcb)
    process, elapsed_ms = run_process(
        command,
        max(30.0, (timeout_ms / 1000.0) * 3.0),
    )
    if process.returncode not in (0, 1):
        raise RuntimeError(
            process.stderr.strip() or process.stdout.strip() or "model discovery failed"
        )
    try:
        document = json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"model discovery output is not JSON: {exc}") from exc
    if document.get("schemaVersion") != "live-ied-model-v1":
        raise RuntimeError("model discovery did not emit live-ied-model-v1")

    output_path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    coverage = document.get("coverage", {})
    warnings = document.get("warnings", [])
    diagnostics = document.get("diagnostics", [])
    structural = str(document.get("structuralFingerprint", document.get("fingerprint", "")))
    runtime = str(document.get("runtimeSnapshotFingerprint", ""))
    accepted = (
        int(coverage.get("logicalDeviceCount", 0) or 0) > 0
        and int(coverage.get("dataAttributeCount", 0) or 0) > 0
        and len(diagnostics) == 0
        and bool(structural)
    )
    return {
        "accepted": accepted,
        "exitCode": process.returncode,
        "elapsedMs": elapsed_ms,
        "structuralFingerprint": structural,
        "runtimeSnapshotFingerprint": runtime,
        "warningCount": len(warnings),
        "diagnosticCount": len(diagnostics),
        "coverage": coverage,
        "modelFile": output_path.name,
        "stderr": process.stderr.strip(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Prove baseline health, inject a post-association read-only MMS timeout, "
            "then prove a fresh direct recovery association and stable structure."
        )
    )
    parser.add_argument("host")
    parser.add_argument("port", nargs="?", type=int, default=102)
    parser.add_argument("--output", required=True)
    parser.add_argument("--binary")
    parser.add_argument("--fault-timeout-ms", type=int, default=3000)
    parser.add_argument("--recovery-timeout-ms", type=int, default=30000)
    parser.add_argument("--settle-ms", type=int, default=500)
    parser.add_argument("--recovery-max-rcb", type=int, default=50)
    args = parser.parse_args()

    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    if args.fault_timeout_ms <= 0 or args.recovery_timeout_ms <= 0:
        parser.error("timeouts must be positive")
    if args.settle_ms < 0:
        parser.error("--settle-ms must be non-negative")
    if args.recovery_max_rcb <= 0:
        parser.error("--recovery-max-rcb must be positive")

    root = pathlib.Path(__file__).resolve().parents[1]
    output_dir = pathlib.Path(args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    binary = find_binary(root, args.binary)

    baseline: dict[str, Any] = {"accepted": False}
    recovery: dict[str, Any] = {"accepted": False}
    fault: dict[str, Any] = {"accepted": False}

    try:
        baseline = run_model(
            binary,
            args.host,
            args.port,
            args.recovery_timeout_ms,
            args.recovery_max_rcb,
            output_dir / "baseline-live-model.json",
        )
        print(
            "Baseline: "
            f"accepted={baseline['accepted']} "
            f"structure={baseline.get('structuralFingerprint', '')} "
            f"runtime={baseline.get('runtimeSnapshotFingerprint', '')} "
            f"warnings={baseline.get('warningCount', 0)}"
        )
        if not baseline["accepted"]:
            raise RuntimeError("healthy baseline model gate failed; refusing fault injection")

        proxy = PostAssociationStallProxy(args.host, args.port)
        proxy.start()
        fault_command = [
            str(binary),
            proxy.evidence.listen_host,
            str(proxy.evidence.listen_port),
            "--timeout-ms",
            str(args.fault_timeout_ms),
            "--no-types",
            "--no-datasets",
            "--no-rcb",
        ]
        fault_started = time.monotonic()
        try:
            fault_process, fault_elapsed_ms = run_process(
                fault_command,
                max(20.0, (args.fault_timeout_ms / 1000.0) * 4.0),
            )
        finally:
            proxy.stop()
        fault_elapsed_ms = int((time.monotonic() - fault_started) * 1000)

        fault_text = process_text = (
            fault_process.stdout
            + (("\n" + fault_process.stderr) if fault_process.stderr else "")
        )
        (output_dir / "fault-injection-client.txt").write_text(
            fault_text,
            encoding="utf-8",
        )
        timeout_observed = bool(TIMEOUT_RE.search(process_text))
        proxy_ok = (
            proxy.evidence.association_response_forwarded
            and proxy.evidence.fault_triggered
            and not proxy.evidence.error
        )
        fault_accepted = proxy_ok and timeout_observed and fault_process.returncode != 0
        fault = {
            "accepted": fault_accepted,
            "kind": "withhold-server-tpkt-after-association",
            "proxyEndpoint": (
                f"{proxy.evidence.listen_host}:{proxy.evidence.listen_port}"
            ),
            "associationResponseForwarded": proxy.evidence.association_response_forwarded,
            "firstPostAssociationResponseWithheld": proxy.evidence.fault_triggered,
            "withheldTpktCount": proxy.evidence.withheld_tpkt_count,
            "withheldBytes": proxy.evidence.withheld_bytes,
            "clientToServerBytes": proxy.evidence.client_to_server_bytes,
            "serverToClientForwardedBytes": proxy.evidence.server_to_client_forwarded_bytes,
            "timeoutObserved": timeout_observed,
            "exitCode": fault_process.returncode,
            "elapsedMs": fault_elapsed_ms,
            "proxyError": proxy.evidence.error,
            "rawFile": "fault-injection-client.txt",
        }
        print(
            "Injected timeout: "
            f"accepted={fault_accepted} "
            f"associationForwarded={fault['associationResponseForwarded']} "
            f"responseWithheld={fault['firstPostAssociationResponseWithheld']} "
            f"timeoutObserved={timeout_observed} "
            f"withheldTpkt={fault['withheldTpktCount']}"
        )

        if args.settle_ms:
            time.sleep(args.settle_ms / 1000.0)

        recovery = run_model(
            binary,
            args.host,
            args.port,
            args.recovery_timeout_ms,
            args.recovery_max_rcb,
            output_dir / "recovery-live-model.json",
        )
        structural_match = (
            baseline.get("structuralFingerprint")
            == recovery.get("structuralFingerprint")
        )
        recovery["matchesBaselineStructuralFingerprint"] = structural_match
        recovery["accepted"] = bool(recovery["accepted"] and structural_match)
        print(
            "Recovery: "
            f"accepted={recovery['accepted']} "
            f"structure={recovery.get('structuralFingerprint', '')} "
            f"matchesBaseline={structural_match} "
            f"warnings={recovery.get('warningCount', 0)}"
        )
    except Exception as exc:
        error = str(exc)
    else:
        error = ""

    accepted = bool(
        baseline.get("accepted")
        and fault.get("accepted")
        and recovery.get("accepted")
    )
    summary = {
        "schemaVersion": "ariec61850-phase4c-timeout-recovery-v1",
        "endpoint": f"{args.host}:{args.port}",
        "binary": os.fspath(binary),
        "readOnly": True,
        "faultTimeoutMs": args.fault_timeout_ms,
        "recoveryTimeoutMs": args.recovery_timeout_ms,
        "baseline": baseline,
        "faultInjection": fault,
        "recovery": recovery,
        "accepted": accepted,
        "error": error,
    }
    summary_path = output_dir / "phase4c-timeout-recovery-summary.json"
    summary_path.write_text(
        json.dumps(summary, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        "Phase 4C controlled timeout/recovery: "
        + ("PASS" if accepted else "FAIL")
        + f" (baseline={baseline.get('accepted', False)}, "
        f"fault={fault.get('accepted', False)}, "
        f"recovery={recovery.get('accepted', False)})"
    )
    print(f"Summary: {summary_path}")
    if error:
        print(f"Error: {error}", file=sys.stderr)
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
