#!/usr/bin/env python3
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET


def creation_flags() -> int:
    if os.name == "nt" and hasattr(subprocess, "CREATE_NO_WINDOW"):
        return int(subprocess.CREATE_NO_WINDOW)
    return 0


def find_binary(build_dir: Path, name: str) -> Path:
    for candidate in (build_dir / name, build_dir / f"{name}.exe"):
        if candidate.is_file():
            return candidate
    for suffix in (name, f"{name}.exe"):
        matches = sorted(build_dir.rglob(suffix))
        if matches:
            return matches[0]
    raise RuntimeError(f"Could not find {name} below {build_dir}")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def run(command: list[str], timeout: int = 15) -> str:
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
        creationflags=creation_flags(),
    )
    output = result.stdout + result.stderr
    print(output, end="")
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {' '.join(command)}\n{output}"
        )
    return output


def verify_fixture(scl: Path) -> None:
    root = ET.parse(scl).getroot()
    namespace = {"scl": "http://www.iec.ch/61850/2003/SCL"}
    controls = root.findall(".//scl:LN0/scl:ReportControl", namespace)
    unconfigured = [
        control for control in controls
        if control.attrib.get("name", "").startswith("Uncfg")
        and control.attrib.get("buffered") == "false"
        and control.attrib.get("indexed") == "false"
        and not control.attrib.get("datSet")
    ]
    indexed_urcb = [
        control for control in controls
        if control.attrib.get("name") == "Unbuffer"
        and control.attrib.get("buffered") == "false"
        and control.attrib.get("indexed") == "true"
    ]
    indexed_brcb = [
        control for control in controls
        if control.attrib.get("name") == "Buffer"
        and control.attrib.get("buffered") == "true"
        and control.attrib.get("indexed") == "true"
    ]
    setting = root.find(".//scl:LN0/scl:SettingControl", namespace)
    if len(unconfigured) != 30:
        raise RuntimeError(f"Golden fixture must contain 30 unconfigured URCBs, got {len(unconfigured)}")
    if len(indexed_urcb) != 1 or len(indexed_brcb) != 1:
        raise RuntimeError("Golden fixture must contain one indexed URCB family and one indexed BRCB family")
    for control in indexed_urcb + indexed_brcb:
        enabled = control.find("scl:RptEnabled", namespace)
        if enabled is None or enabled.attrib.get("max") != "2":
            raise RuntimeError("Indexed golden RCB family must have RptEnabled max=2")
    if setting is None or setting.attrib.get("numOfSGs") != "4" or setting.attrib.get("actSG") != "1":
        raise RuntimeError("Golden fixture must contain SettingControl numOfSGs=4 actSG=1")


def require_read(read_probe: Path, port: int, item: str, expected: str) -> str:
    output = run([
        str(read_probe), "127.0.0.1", str(port),
        "--domain", "IEDGOLDENLD0", "--item", item,
        "--timeout-ms", "5000",
    ])
    if expected not in output:
        raise RuntimeError(f"Read evidence for {item} did not contain {expected!r}:\n{output}")
    return output


def update_manifest_value(manifest_path: Path, item: str, new_value: str) -> int:
    text = manifest_path.read_text(encoding="utf-8")
    lines = text.splitlines()
    if not lines or not lines[0].startswith("ARSTACK_IED_MODEL\t2\t"):
        raise RuntimeError("Golden simulator manifest header is missing")
    header = lines[0].split("\t")
    revision = int(header[2]) + 1
    lines[0] = f"ARSTACK_IED_MODEL\t2\t{revision}"

    prefix = f"OBJ\tIEDGOLDENLD0\t{item}\t"
    changed = False
    for index, line in enumerate(lines[1:], start=1):
        if not line.startswith(prefix):
            continue
        fields = line.split("\t")
        if len(fields) < 6:
            raise RuntimeError(f"Malformed golden OBJ line for {item}: {line}")
        fields[5] = new_value
        lines[index] = "\t".join(fields)
        changed = True
        break
    if not changed:
        raise RuntimeError(f"Golden manifest object not found: {item}")

    temporary = manifest_path.with_suffix(manifest_path.suffix + ".golden-new")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    os.replace(temporary, manifest_path)
    return revision


def run_brcb_event_probe(
    brcb_probe: Path,
    port: int,
    manifest_path: Path,
) -> str:
    """Enable indexed BRCB, mutate one DataSet member, require buffered report."""
    process = subprocess.Popen(
        [
            str(brcb_probe), "127.0.0.1", str(port),
            "--domain", "IEDGOLDENLD0",
            "--rcb", "LLN0$BR$Buffer01",
            "--timeout-ms", "9000",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        creationflags=creation_flags(),
    )
    try:
        if process.stdout is None:
            raise RuntimeError("Golden BRCB probe stdout pipe was not created")
        with ThreadPoolExecutor(max_workers=1) as executor:
            ready = executor.submit(process.stdout.readline).result(timeout=7).strip()
        print(ready)
        if "MMS_BRCB_EVENT_READY" not in ready:
            stderr = process.stderr.read() if process.stderr is not None else ""
            raise RuntimeError(
                "Golden BRCB did not become ready after RptEna=true: "
                f"stdout={ready!r} stderr={stderr!r}"
            )

        revision = update_manifest_value(
            manifest_path,
            "XCBR1$ST$Pos$stVal",
            "true",
        )
        remaining_stdout, stderr = process.communicate(timeout=13)
        output = ready + "\n" + remaining_stdout
        print(remaining_stdout, end="")
        if stderr:
            print(stderr, end="")
        if process.returncode != 0 or "MMS_BRCB_EVENT_PASS" not in output:
            raise RuntimeError(
                "Golden BRCB buffered InformationReport regression failed: "
                f"revision={revision} exit={process.returncode} "
                f"stdout={output!r} stderr={stderr!r}"
            )
        if "rptid=IEDGOLDENLD0/LLN0$BR$Buffer" not in output:
            raise RuntimeError("Golden BRCB report changed the unsuffixed RptID identity")
        return output
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--scl", required=True, type=Path)
    args = parser.parse_args()

    app = args.app.resolve()
    build_dir = args.build_dir.resolve()
    scl = args.scl.resolve()
    if not app.is_file() or not scl.is_file():
        raise RuntimeError("Golden acceptance requires an existing app and SCL fixture")
    verify_fixture(scl)

    read_probe = find_binary(build_dir, "ariec61850_mms_read_probe")
    urcb_probe = find_binary(build_dir, "ariec61850_mms_urcb_gi_probe")
    brcb_probe = find_binary(build_dir, "ariec61850_mms_brcb_event_probe")
    sgcb_probe = find_binary(build_dir, "ariec61850_mms_sgcb_probe")

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-iedscout-golden-") as temp:
        log_path = Path(temp) / "golden-runtime.log"
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        environment["ARSTACK_IEDSIM_TRACE_SERVER"] = "1"
        with log_path.open("w+", encoding="utf-8") as log:
            process = subprocess.Popen(
                [
                    str(app), "--scl", str(scl), "--runtime", "--port", str(port),
                    "--exit-after-ms", "45000",
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
                env=environment,
                creationflags=creation_flags(),
            )
            manifest_path = Path(tempfile.gettempdir()) / f"arstack-ied-simulator-{process.pid}.model"
            try:
                deadline = time.monotonic() + 15.0
                ready_text = ""
                manifest_text = ""
                while time.monotonic() < deadline:
                    log.flush()
                    ready_text = log_path.read_text(encoding="utf-8", errors="replace")
                    try:
                        manifest_text = manifest_path.read_text(encoding="utf-8")
                    except (FileNotFoundError, PermissionError, UnicodeDecodeError):
                        manifest_text = ""
                    if (
                        "IEDSIM_EVENT kind=server_ready" in ready_text
                        and "RCB\tIEDGOLDENLD0\tLLN0$BR$Buffer01\t1\t" in manifest_text
                        and "OBJ\tIEDGOLDENLD0\tXCBR1$ST$Pos$stVal\t" in manifest_text
                    ):
                        break
                    if process.poll() is not None:
                        break
                    time.sleep(0.05)
                else:
                    ready_text = log_path.read_text(encoding="utf-8", errors="replace")

                required_ready = (
                    "urcbs=32",
                    "brcbs=2",
                    "sgcbs=1",
                    "omitted_urcbs=0",
                    "omitted_brcbs=0",
                )
                if "IEDSIM_EVENT kind=server_ready" not in ready_text or any(
                    token not in ready_text for token in required_ready
                ):
                    raise RuntimeError(
                        "Golden runtime readiness/count evidence is incomplete:\n" + ready_text
                    )
                if not manifest_text:
                    raise RuntimeError("Golden runtime manifest was not available for event injection")

                # First/last dense unconfigured URCBs must survive manifest projection
                # and be MMS-readable, proving they were not dropped by capacity logic.
                require_read(read_probe, port, "LLN0$RP$Uncfg01$RptEna", "value=false")
                require_read(read_probe, port, "LLN0$RP$Uncfg30$RptEna", "value=false")

                # Indexed instances are concrete MMS ObjectNames while RptID remains
                # the unsuffixed SCL family identity used by IEDScout.
                require_read(
                    read_probe, port, "LLN0$RP$Unbuffer01$RptID",
                    "IEDGOLDENLD0/LLN0$RP$Unbuffer",
                )
                require_read(
                    read_probe, port, "LLN0$BR$Buffer01$RptID",
                    "IEDGOLDENLD0/LLN0$BR$Buffer",
                )

                urcb = run([
                    str(urcb_probe), "127.0.0.1", str(port),
                    "--domain", "IEDGOLDENLD0",
                    "--rcb", "LLN0$RP$Unbuffer01",
                    "--timeout-ms", "7000",
                ], timeout=12)
                if (
                    "MMS_URCB_GI_PASS" not in urcb
                    or "rptid=IEDGOLDENLD0/LLN0$RP$Unbuffer" not in urcb
                ):
                    raise RuntimeError("Indexed URCB GI evidence is incomplete:\n" + urcb)

                brcb = run_brcb_event_probe(brcb_probe, port, manifest_path)
                if "MMS_BRCB_EVENT_PASS" not in brcb:
                    raise RuntimeError("Indexed BRCB report evidence is incomplete:\n" + brcb)

                first_sg = run([
                    str(sgcb_probe), "127.0.0.1", str(port),
                    "--domain", "IEDGOLDENLD0", "--root", "LLN0$SP$SGCB",
                    "--expect-num", "4", "--expect-act", "1", "--activate", "3",
                ])
                if "SGCB_PROBE_PASS discovery=5/5 num=4 initial=1 final=3" not in first_sg:
                    raise RuntimeError("Golden SGCB activation evidence is incomplete:\n" + first_sg)

                second_sg = run([
                    str(sgcb_probe), "127.0.0.1", str(port),
                    "--domain", "IEDGOLDENLD0", "--root", "LLN0$SP$SGCB",
                    "--expect-num", "4", "--expect-act", "3", "--reject", "5",
                ])
                if "SGCB_PROBE_PASS discovery=5/5 num=4 initial=3 final=3" not in second_sg:
                    raise RuntimeError("Golden SGCB cross-association evidence is incomplete:\n" + second_sg)

                log.flush()
                final_log = log_path.read_text(encoding="utf-8", errors="replace")
                if "IEDSIM_EVENT kind=client_error" in final_log:
                    raise RuntimeError("Golden runtime emitted client_error:\n" + final_log)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)

    print(
        "IEDSCOUT_GOLDEN_SYNTHETIC_PASS "
        "rcb_runtime=34 unconfigured_urcb=30 indexed_urcb=2 indexed_brcb=2 "
        "sgcb=1 omitted_urcbs=0 omitted_brcbs=0 "
        "urcb_gi=pass brcb_report=pass sgcb_cross_association=pass"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
