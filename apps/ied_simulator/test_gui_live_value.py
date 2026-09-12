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


def resolve_named_probe(argument: str, stem: str, label: str) -> str:
    path = Path(argument)
    names = {stem, f"{stem}.exe"}
    if path.is_file() and path.name in names:
        return str(path)
    if path.is_dir():
        matches = sorted(
            candidate for candidate in path.rglob(f"{stem}*")
            if candidate.is_file() and candidate.name in names
        )
        if matches:
            return str(matches[0])
    raise FileNotFoundError(f"{label} not found under {path}")


def resolve_read_probe(argument: str) -> str:
    return resolve_named_probe(argument, "ariec61850_mms_read_probe", "MMS read probe")


def resolve_urcb_probe(argument: str) -> str:
    return resolve_named_probe(argument, "ariec61850_mms_urcb_gi_probe", "MMS URCB GI probe")


def resolve_brcb_probe(argument: str) -> str:
    return resolve_named_probe(argument, "ariec61850_mms_brcb_event_probe", "MMS BRCB event probe")


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


def run_urcb_gi_probe(urcb_probe: str, port: int) -> str:
    result = subprocess.run(
        [
            urcb_probe,
            "127.0.0.1",
            str(port),
            "--domain",
            "MU01LD0",
            "--rcb",
            "LLN0$RP$URCB01",
            "--timeout-ms",
            "5000",
        ],
        capture_output=True,
        text=True,
        timeout=9,
        check=False,
        creationflags=creation_flags(),
    )
    if result.returncode != 0 or "MMS_URCB_GI_PASS" not in result.stdout:
        raise RuntimeError(
            "URCB enable/GI/InformationReport regression failed: "
            f"exit={result.returncode} stdout={result.stdout!r} stderr={result.stderr!r}"
        )
    return result.stdout.strip()


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


def run_brcb_event_probe(
    brcb_probe: str,
    port: int,
    manifest_path: Path,
) -> str:
    """Enable BRCB, mutate one DataSet member, and require buffered report delivery."""
    process = subprocess.Popen(
        [
            brcb_probe,
            "127.0.0.1",
            str(port),
            "--domain",
            "MU01LD0",
            "--rcb",
            "LLN0$BR$BRCB01",
            "--timeout-ms",
            "6000",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=creation_flags(),
    )
    try:
        if process.stdout is None:
            raise RuntimeError("BRCB probe stdout pipe was not created")
        with ThreadPoolExecutor(max_workers=1) as executor:
            ready = executor.submit(process.stdout.readline).result(timeout=6).strip()
        if "MMS_BRCB_EVENT_READY" not in ready:
            stderr = process.stderr.read() if process.stderr is not None else ""
            raise RuntimeError(
                "BRCB did not become ready after RptEna=true: "
                f"stdout={ready!r} stderr={stderr!r}"
            )

        update_manifest_value(manifest_path, "XCBR1$ST$Pos$stVal", "true")
        remaining_stdout, stderr = process.communicate(timeout=9)
        output = ready + "\n" + remaining_stdout
        if process.returncode != 0 or "MMS_BRCB_EVENT_PASS" not in output:
            raise RuntimeError(
                "BRCB buffered InformationReport regression failed: "
                f"exit={process.returncode} stdout={output!r} stderr={stderr!r}"
            )
        return output.strip()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)


def resolve_control_probe(argument: str) -> str:
    return resolve_named_probe(argument, "ariec61850_control_interop_probe", "control interoperability probe")


def run_direct_normal_control_regression(
    control_probe: str,
    read_probe: str,
    port: int,
) -> str:
    common = [
        control_probe,
        "127.0.0.1",
        str(port),
        "--object",
        "MU01LD0/GGIO1.SPCSO1",
        "--timeout-ms",
        "5000",
    ]
    discovery = subprocess.run(
        common,
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
        creationflags=creation_flags(),
    )
    if (
        discovery.returncode != 0
        or "ctlModel=direct-normal" not in discovery.stdout
        or "cdc=SPC" not in discovery.stdout
        or "STATUS_BEFORE false" not in discovery.stdout
        or "status=DISCOVERY_PASS" not in discovery.stdout
    ):
        raise RuntimeError(
            "configured Direct-Normal discovery failed: "
            f"exit={discovery.returncode} stdout={discovery.stdout!r} stderr={discovery.stderr!r}"
        )

    # Prove fail-closed Check handling before the accepted command.
    rejected = subprocess.run(
        common
        + [
            "--action",
            "operate",
            "--value",
            "on",
            "--value-kind",
            "bool",
            "--arm",
            "IEC61850-LAB-CONTROL",
        ],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
        creationflags=creation_flags(),
    )
    if (
        rejected.returncode != 4
        or "accepted=false" not in rejected.stdout
        or "mmsFailure=11:object-value-invalid" not in rejected.stdout
        or "STATUS_AFTER false" not in rejected.stdout
    ):
        raise RuntimeError(
            "Direct-Normal fail-closed check-bit regression failed: "
            f"exit={rejected.returncode} stdout={rejected.stdout!r} stderr={rejected.stderr!r}"
        )

    accepted = subprocess.run(
        common
        + [
            "--action",
            "operate",
            "--value",
            "on",
            "--value-kind",
            "bool",
            "--interlock-check",
            "off",
            "--synchro-check",
            "off",
            "--arm",
            "IEC61850-LAB-CONTROL",
        ],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
        creationflags=creation_flags(),
    )
    if (
        accepted.returncode != 0
        or "completion=accepted" not in accepted.stdout
        or "accepted=true" not in accepted.stdout
        or "STATUS_AFTER true" not in accepted.stdout
        or "NO_RETRY_EVIDENCE controlWrites=1" not in accepted.stdout
    ):
        raise RuntimeError(
            "Direct-Normal Oper wire regression failed: "
            f"exit={accepted.returncode} stdout={accepted.stdout!r} stderr={accepted.stderr!r}"
        )

    status = run_probe(read_probe, port, "GGIO1$ST$SPCSO1$stVal")
    if status.returncode != 0 or "value=true" not in status.stdout:
        raise RuntimeError(
            "Direct-Normal process status did not persist for a second external association: "
            f"exit={status.returncode} stdout={status.stdout!r} stderr={status.stderr!r}"
        )
    return discovery.stdout.strip() + "\n" + rejected.stdout.strip() + "\n" + accepted.stdout.strip()


def run_control_action(
    control_probe: str,
    port: int,
    object_reference: str,
    action: str,
    value: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            control_probe,
            "127.0.0.1",
            str(port),
            "--object",
            object_reference,
            "--timeout-ms",
            "5000",
            "--termination-timeout-ms",
            "5000",
            "--action",
            action,
            "--value",
            value,
            "--value-kind",
            "bool",
            "--interlock-check",
            "off",
            "--synchro-check",
            "off",
            "--arm",
            "IEC61850-LAB-CONTROL",
        ],
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
        creationflags=creation_flags(),
    )


def run_sbo_enhanced_control_regressions(
    control_probe: str,
    read_probe: str,
    port: int,
) -> str:
    outputs: list[str] = []

    # SBO normal: Select/Cancel must be non-mutating; Select/Operate must mutate.
    sbo_cancel = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO2", "select-cancel", "on")
    if (
        sbo_cancel.returncode != 0
        or "ctlModel=sbo-normal" not in sbo_cancel.stdout
        or "completion=accepted" not in sbo_cancel.stdout
        or "STATUS_AFTER false" not in sbo_cancel.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO2$Cancel" not in sbo_cancel.stdout
    ):
        raise RuntimeError(
            "SBO-normal Select/Cancel wire regression failed: "
            f"exit={sbo_cancel.returncode} stdout={sbo_cancel.stdout!r} stderr={sbo_cancel.stderr!r}"
        )
    outputs.append(sbo_cancel.stdout.strip())

    sbo_oper = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO2", "select-operate", "on")
    if (
        sbo_oper.returncode != 0
        or "ctlModel=sbo-normal" not in sbo_oper.stdout
        or "completion=accepted" not in sbo_oper.stdout
        or "STATUS_AFTER true" not in sbo_oper.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO2$Oper" not in sbo_oper.stdout
    ):
        raise RuntimeError(
            "SBO-normal Select/Operate wire regression failed: "
            f"exit={sbo_oper.returncode} stdout={sbo_oper.stdout!r} stderr={sbo_oper.stderr!r}"
        )
    outputs.append(sbo_oper.stdout.strip())

    # Direct enhanced: confirmed Write is not completion; positive correlated
    # CommandTermination is mandatory and no automatic retry is allowed.
    direct_enhanced = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO3", "operate", "on")
    if (
        direct_enhanced.returncode != 0
        or "ctlModel=direct-enhanced" not in direct_enhanced.stdout
        or "completion=positive-termination" not in direct_enhanced.stdout
        or "termination=true" not in direct_enhanced.stdout
        or "STATUS_AFTER true" not in direct_enhanced.stdout
        or "NO_RETRY_EVIDENCE controlWrites=1" not in direct_enhanced.stdout
    ):
        raise RuntimeError(
            "Direct-enhanced CommandTermination regression failed: "
            f"exit={direct_enhanced.returncode} stdout={direct_enhanced.stdout!r} stderr={direct_enhanced.stderr!r}"
        )
    outputs.append(direct_enhanced.stdout.strip())

    # SBO enhanced: first prove SBOw/Cancel is non-mutating, then prove exact
    # SBOw -> Oper -> positive CommandTermination sequence.
    enhanced_cancel = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO4", "select-cancel", "on")
    if (
        enhanced_cancel.returncode != 0
        or "ctlModel=sbo-enhanced" not in enhanced_cancel.stdout
        or "STATUS_AFTER false" not in enhanced_cancel.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$SBOw" not in enhanced_cancel.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$Cancel" not in enhanced_cancel.stdout
    ):
        raise RuntimeError(
            "SBO-enhanced SBOw/Cancel regression failed: "
            f"exit={enhanced_cancel.returncode} stdout={enhanced_cancel.stdout!r} stderr={enhanced_cancel.stderr!r}"
        )
    outputs.append(enhanced_cancel.stdout.strip())

    enhanced_oper = run_control_action(
        control_probe, port, "MU01LD0/GGIO1.SPCSO4", "select-operate", "on")
    if (
        enhanced_oper.returncode != 0
        or "ctlModel=sbo-enhanced" not in enhanced_oper.stdout
        or "completion=positive-termination" not in enhanced_oper.stdout
        or "termination=true" not in enhanced_oper.stdout
        or "STATUS_AFTER true" not in enhanced_oper.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$SBOw" not in enhanced_oper.stdout
        or "write=MU01LD0/GGIO1$CO$SPCSO4$Oper" not in enhanced_oper.stdout
        or "NO_RETRY_EVIDENCE controlWrites=2" not in enhanced_oper.stdout
    ):
        raise RuntimeError(
            "SBO-enhanced SBOw/Oper/CommandTermination regression failed: "
            f"exit={enhanced_oper.returncode} stdout={enhanced_oper.stdout!r} stderr={enhanced_oper.stderr!r}"
        )
    outputs.append(enhanced_oper.stdout.strip())

    for item in ("SPCSO2", "SPCSO3", "SPCSO4"):
        status = run_probe(read_probe, port, f"GGIO1$ST${item}$stVal")
        if status.returncode != 0 or "value=true" not in status.stdout:
            raise RuntimeError(
                f"{item} status was not persistent across a second association: "
                f"exit={status.returncode} stdout={status.stdout!r} stderr={status.stderr!r}"
            )
    return "\n".join(outputs)


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
    urcb_probe = resolve_urcb_probe(args.read_probe)
    brcb_probe = resolve_brcb_probe(args.read_probe)
    control_probe = resolve_control_probe(args.read_probe)

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
                "45000",
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
                urcb_manifest = (
                    "RCB\tMU01LD0\tLLN0$RP$URCB01\t0\t"
                    "MU01LD0/LLN0$RP$URCB01\tMU01LD0\tLLN0$dsGO\t1\t100\t1000\t100\t120\t128"
                )
                brcb_manifest = (
                    "RCB\tMU01LD0\tLLN0$BR$BRCB01\t1\t"
                    "MU01LD0/LLN0$BR$BRCB01\tMU01LD0\tLLN0$dsGO\t2\t75\t1000\t108\t121\t128"
                )
                configured_controls = (
                    "CTL\tMU01LD0\tGGIO1\tSPCSO1\tSPC\t1",
                    "CTL\tMU01LD0\tGGIO1\tSPCSO2\tSPC\t2",
                    "CTL\tMU01LD0\tGGIO1\tSPCSO3\tSPC\t3",
                    "CTL\tMU01LD0\tGGIO1\tSPCSO4\tSPC\t4",
                )
                if (
                    manifest_text.startswith("ARSTACK_IED_MODEL\t2\t2\n")
                    and mapped_value in manifest_text
                    and structural_only_value in manifest_text
                    and "XCBR1$ST$Pos$q\tQuality\tQuality\tgood" in manifest_text
                    and "XCBR1$ST$Pos$t\tTimestamp\tTimestamp\t" in manifest_text
                    and urcb_manifest in manifest_text
                    and brcb_manifest in manifest_text
                    and all(control in manifest_text for control in configured_controls)
                    and "GGIO1$CF$SPCSO1$ctlModel\tEnum\tEnumeration\t1" in manifest_text
                    and "GGIO1$CF$SPCSO2$ctlModel\tEnum\tEnumeration\t2" in manifest_text
                    and "GGIO1$CF$SPCSO3$ctlModel\tEnum\tEnumeration\t3" in manifest_text
                    and "GGIO1$CF$SPCSO4$ctlModel\tEnum\tEnumeration\t4" in manifest_text
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError(
                    "GUI did not publish revision 2 with edited/full model leaves, reporting metadata, and configured control metadata"
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

            control_output = run_direct_normal_control_regression(
                control_probe,
                read_probe,
                port,
            )
            sbo_enhanced_output = run_sbo_enhanced_control_regressions(
                control_probe,
                read_probe,
                port,
            )
            concurrent_seconds = prove_concurrent_associations(
                read_probe,
                port,
                "TCTR1$MX$Amp$instMag$i",
            )
            urcb_output = run_urcb_gi_probe(urcb_probe, port)
            brcb_output = run_brcb_event_probe(
                brcb_probe,
                port,
                manifest_path,
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

            app.wait(timeout=47)
            print(
                "IEDSIM_GUI_LIVE_VALUE_PASS "
                "edited=MU01LD0/TCTR1$MX$Amp$instMag$i:42 "
                "structural=MU01LD0/TCTR1$MX$AmpUnmapped$instMag$i:0 "
                f"concurrent_association_seconds={concurrent_seconds:.3f} "
                "control_direct_normal=pass "
                "control_sbo_normal=pass "
                "control_direct_enhanced=pass "
                "control_sbo_enhanced=pass "
                "urcb_gi=pass "
                "brcb_event=pass "
                "quality=same-association:030000->03C110 "
                "timestamp=same-association:0->1700000000123"
            )
            print(control_output)
            print(sbo_enhanced_output)
            print(urcb_output)
            print(brcb_output)
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
