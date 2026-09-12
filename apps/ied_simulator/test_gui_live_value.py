#!/usr/bin/env python3
"""Launch the Qt simulator and prove GUI/live SCL model values are visible over MMS."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
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
            candidate for candidate in path.rglob("ariec61850_mms_read_probe*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0])
    raise FileNotFoundError(f"MMS read probe not found under {path}")


def probe_command(read_probe: str, port: int, item: str) -> list[str]:
    return [
        read_probe,
        "127.0.0.1",
        str(port),
        "--domain",
        "MU01LD0",
        "--item",
        item,
    ]


def run_probe(read_probe: str, port: int, item: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        probe_command(read_probe, port, item)
        + ["--timeout-ms", "3000"],
        capture_output=True,
        text=True,
        timeout=6,
        check=False,
        creationflags=creation_flags(),
    )


def update_manifest_value(manifest_path: Path, item: str, new_value: str) -> int:
    text = manifest_path.read_text(encoding="utf-8")
    lines = text.splitlines()
    if not lines or not lines[0].startswith("ARSTACK_IED_MODEL\t2\t"):
        raise RuntimeError("simulator manifest header is missing")
    header = lines[0].split("\t")
    revision = int(header[2]) + 1
    lines[0] = f"ARSTACK_IED_MODEL\t2\t{revision}"

    prefix = f"OBJ\tMU01LD0\t{item}\t"
    changed = False
    for index, line in enumerate(lines[1:], start=1):
        if not line.startswith(prefix):
            continue
        fields = line.split("\t")
        if len(fields) < 6:
            raise RuntimeError(f"malformed OBJ line for {item}: {line}")
        fields[5] = new_value
        lines[index] = "\t".join(fields)
        changed = True
        break
    if not changed:
        raise RuntimeError(f"manifest object not found: {item}")

    temporary = manifest_path.with_suffix(manifest_path.suffix + ".test-new")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    os.replace(temporary, manifest_path)
    return revision


def prove_concurrent_associations(read_probe: str, port: int, item: str) -> float:
    """Keep association A open and require association B to finish meanwhile."""
    holder = subprocess.Popen(
        probe_command(read_probe, port, item)
        + [
            "--count",
            "2",
            "--delay-ms",
            "4000",
            "--timeout-ms",
            "3000",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=creation_flags(),
    )
    try:
        if holder.stdout is None:
            raise RuntimeError("concurrency holder stdout pipe was not created")
        with ThreadPoolExecutor(max_workers=1) as executor:
            first_line = executor.submit(holder.stdout.readline).result(timeout=5).strip()
        if "value=42" not in first_line:
            stderr = holder.stderr.read() if holder.stderr is not None else ""
            raise RuntimeError(
                "first concurrent association did not establish: "
                f"stdout={first_line!r} stderr={stderr!r}"
            )

        started = time.monotonic()
        try:
            second = subprocess.run(
                probe_command(read_probe, port, item)
                + ["--timeout-ms", "1500"],
                capture_output=True,
                text=True,
                timeout=2.5,
                check=False,
                creationflags=creation_flags(),
            )
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(
                "second MMS association was blocked by the first association"
            ) from error
        elapsed = time.monotonic() - started
        if second.returncode != 0 or "value=42" not in second.stdout:
            raise RuntimeError(
                "second concurrent association failed: "
                f"exit={second.returncode} stdout={second.stdout!r} stderr={second.stderr!r}"
            )
        if elapsed >= 2.5:
            raise RuntimeError(
                f"second association completed too slowly for concurrency proof: {elapsed:.3f}s"
            )

        remaining_stdout, stderr = holder.communicate(timeout=7)
        holder_output = first_line + "\n" + remaining_stdout
        if holder.returncode != 0 or holder_output.count("value=42") < 2:
            raise RuntimeError(
                "held association did not remain healthy: "
                f"exit={holder.returncode} stdout={holder_output!r} stderr={stderr!r}"
            )
        return elapsed
    finally:
        if holder.poll() is None:
            holder.kill()
            holder.wait(timeout=3)


def prove_same_association_refresh(
    read_probe: str,
    port: int,
    manifest_path: Path,
    item: str,
    initial_fragment: str,
    updated_value: str,
    updated_fragment: str,
) -> str:
    process = subprocess.Popen(
        probe_command(read_probe, port, item)
        + [
            "--count",
            "2",
            "--delay-ms",
            "1200",
            "--timeout-ms",
            "3000",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=creation_flags(),
    )
    try:
        if process.stdout is None:
            raise RuntimeError("read probe stdout pipe was not created")
        with ThreadPoolExecutor(max_workers=1) as executor:
            first_line = executor.submit(process.stdout.readline).result(timeout=5).strip()
        if initial_fragment not in first_line:
            stderr = process.stderr.read() if process.stderr is not None else ""
            raise RuntimeError(
                f"first read for {item} did not expose {initial_fragment!r}: "
                f"stdout={first_line!r} stderr={stderr!r}"
            )

        update_manifest_value(manifest_path, item, updated_value)
        remaining_stdout, stderr = process.communicate(timeout=7)
        output = first_line + "\n" + remaining_stdout
        if process.returncode != 0 or updated_fragment not in output:
            raise RuntimeError(
                f"same-association refresh failed for {item}: exit={process.returncode} "
                f"stdout={output!r} stderr={stderr!r}"
            )
        return output
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)


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
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as app_log:
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
                "--exit-after-ms",
                "30000",
            ],
            stdout=app_log,
            stderr=subprocess.STDOUT,
            text=True,
            env=environment,
            creationflags=creation_flags(),
        )
        last_error = "server not ready"
        try:
            manifest_path = (
                Path(tempfile.gettempdir()) / f"arstack-ied-simulator-{app.pid}.model"
            )
            manifest_deadline = time.monotonic() + 6.0
            while time.monotonic() < manifest_deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                try:
                    manifest_text = manifest_path.read_text(encoding="utf-8")
                except (FileNotFoundError, PermissionError, UnicodeDecodeError):
                    manifest_text = ""
                mapped_value = "TCTR1$MX$Amp$instMag$i\tINT32\tNumber\t42"
                structural_only_value = (
                    "TCTR1$MX$AmpUnmapped$instMag$i\tINT32\tNumber\t0"
                )
                if (
                    manifest_text.startswith("ARSTACK_IED_MODEL\t2\t2\n")
                    and mapped_value in manifest_text
                    and structural_only_value in manifest_text
                    and "XCBR1$ST$Pos$q\tQuality\tQuality\tgood" in manifest_text
                    and "XCBR1$ST$Pos$t\tTimestamp\tTimestamp\t" in manifest_text
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError(
                    "GUI did not publish revision 2 with the edited DataSet leaf, "
                    "structural-only leaf, Quality, and Timestamp objects"
                )

            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                time.sleep(0.25)
                mapped_probe = run_probe(
                    read_probe,
                    port,
                    "TCTR1$MX$Amp$instMag$i",
                )
                if mapped_probe.returncode != 0 or "value=42" not in mapped_probe.stdout:
                    last_error = (
                        "mapped leaf: "
                        f"exit={mapped_probe.returncode} stdout={mapped_probe.stdout} "
                        f"stderr={mapped_probe.stderr}"
                    )
                    time.sleep(0.2)
                    continue

                structural_probe = run_probe(
                    read_probe,
                    port,
                    "TCTR1$MX$AmpUnmapped$instMag$i",
                )
                if structural_probe.returncode == 0 and "value=0" in structural_probe.stdout:
                    break
                last_error = (
                    "structural-only leaf: "
                    f"exit={structural_probe.returncode} stdout={structural_probe.stdout} "
                    f"stderr={structural_probe.stderr}"
                )
                time.sleep(0.2)
            else:
                raise RuntimeError(last_error)

            concurrent_seconds = prove_concurrent_associations(
                read_probe,
                port,
                "TCTR1$MX$Amp$instMag$i",
            )
            quality_output = prove_same_association_refresh(
                read_probe,
                port,
                manifest_path,
                "XCBR1$ST$Pos$q",
                "value=030000",
                "questionable,old-data,test",
                "value=03C110",
            )
            timestamp_output = prove_same_association_refresh(
                read_probe,
                port,
                manifest_path,
                "XCBR1$ST$Pos$t",
                "value=unix-ms=0 UTC",
                "1700000000123",
                "value=unix-ms=1700000000123 UTC",
            )

            app.wait(timeout=32)
            print(
                "IEDSIM_GUI_LIVE_VALUE_PASS "
                "edited=MU01LD0/TCTR1$MX$Amp$instMag$i:42 "
                "structural=MU01LD0/TCTR1$MX$AmpUnmapped$instMag$i:0 "
                f"concurrent_association_seconds={concurrent_seconds:.3f} "
                "quality=same-association:030000->03C110 "
                "timestamp=same-association:0->1700000000123"
            )
            print(quality_output.strip())
            print(timestamp_output.strip())
            return 0
        except BaseException:
            if app.poll() is None:
                try:
                    app.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=5)
            app_log.seek(0)
            output = app_log.read().strip()
            raise RuntimeError(
                f"GUI live-value test failed: {last_error}; app_output={output}"
            )


if __name__ == "__main__":
    raise SystemExit(main())
