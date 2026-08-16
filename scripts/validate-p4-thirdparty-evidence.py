#!/usr/bin/env python3
"""Validate P4 third-party interoperability evidence without promoting OMICRON claims."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


PASS_MARKER = "IEDSIM_P4_THIRDPARTY_INTEROP_PASS"


def fail(message: str) -> None:
    raise RuntimeError(message)


def tshark_count(pcap: Path, port: int, display_filter: str) -> int:
    command = [
        "tshark",
        "-d",
        f"tcp.port=={port},tpkt",
        "-r",
        str(pcap),
        "-Y",
        display_filter,
        "-T",
        "fields",
        "-e",
        "frame.number",
    ]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        return 0
    return sum(1 for line in result.stdout.splitlines() if line.strip())


def validate_wire_trace(path: Path) -> dict[str, int | str]:
    if not path.is_file() or path.stat().st_size < 512:
        fail("socket syscall wire trace is absent or too small")

    text = path.read_text(encoding="utf-8", errors="replace")
    lines = [line for line in text.splitlines() if line.strip()]
    send_lines = [
        line for line in lines
        if re.search(r"\b(send|sendto|sendmsg)\(", line)
    ]
    recv_lines = [
        line for line in lines
        if re.search(r"\b(recv|recvfrom|recvmsg)\(", line)
    ]
    tpkt_lines = [line for line in lines if "\\x03\\x00" in line]

    if len(send_lines) < 10:
        fail(f"insufficient outbound socket wire evidence: {len(send_lines)} calls")
    if len(recv_lines) < 10:
        fail(f"insufficient inbound socket wire evidence: {len(recv_lines)} calls")
    if len(tpkt_lines) < 10:
        fail(f"insufficient TPKT-shaped syscall evidence: {len(tpkt_lines)} calls")

    return {
        "path": path.name,
        "bytes": path.stat().st_size,
        "sendCalls": len(send_lines),
        "recvCalls": len(recv_lines),
        "tpktCalls": len(tpkt_lines),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-log", required=True)
    parser.add_argument("--server-log", required=True)
    parser.add_argument("--state", required=True)
    parser.add_argument("--pcap", required=True)
    parser.add_argument("--wire-trace", required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    client_log = Path(args.client_log).read_text(encoding="utf-8", errors="replace")
    server_log = Path(args.server_log).read_text(encoding="utf-8", errors="replace")
    pcap = Path(args.pcap)
    wire_trace = Path(args.wire_trace)
    state_path = Path(args.state)

    if PASS_MARKER not in client_log:
        fail("independent libiec61850 PASS marker missing")
    for token in (
        "association=true",
        "discovery=true",
        "read42=true",
        "urcb_gi=true",
        "brcb=true",
        "external_write=true",
        "controls=1,2,3,4",
        "termination=2",
        "status_readback=true",
    ):
        if token not in client_log:
            fail(f"client evidence token missing: {token}")

    for token in (
        "kind=report_sent",
        "kind=brcb_report_sent",
        "kind=control_operate",
        "kind=control_termination",
        "object=MU01LD0/GGIO1.SPCSO3",
        "object=MU01LD0/GGIO1.SPCSO4",
    ):
        if token not in server_log:
            fail(f"server runtime evidence missing: {token}")
    if server_log.count("kind=control_operate") < 4:
        fail("fewer than four successful control operate events")
    if server_log.count("kind=control_termination") < 2:
        fail("fewer than two enhanced CommandTermination events")
    if server_log.count("kind=mms_service") < 20:
        fail("insufficient server-side MMS service trace")
    for service in ("GetNameList", "Read", "Write", "GetVariableAccessAttributes"):
        if f"service={service}" not in server_log:
            fail(f"server-side MMS trace missing service={service}")

    states = json.loads(state_path.read_text(encoding="utf-8"))
    if not isinstance(states, list):
        fail("state dump is not a list")
    by_item = {str(item.get("mmsItem", "")): item for item in states if isinstance(item, dict)}
    for index in range(1, 5):
        item = f"GGIO1$ST$SPCSO{index}$stVal"
        state = by_item.get(item)
        if state is None:
            fail(f"state dump missing {item}")
        if str(state.get("value", "")).lower() != "true":
            fail(f"control status not true for {item}: {state}")
        if state.get("origin") != "mms-control":
            fail(f"control status origin mismatch for {item}: {state.get('origin')}")

    measurement = by_item.get("TCTR1$MX$Amp$instMag$i")
    if measurement is None or str(measurement.get("value")) != "42":
        fail(f"P1 live measurement missing/incorrect: {measurement}")

    tcp_frames = 0
    tpkt_frames = 0
    mms_frames = 0
    pcap_meaningful = pcap.is_file() and pcap.stat().st_size >= 512
    if pcap_meaningful:
        tcp_frames = tshark_count(pcap, args.port, f"tcp.port == {args.port}")
        tpkt_frames = tshark_count(pcap, args.port, "tpkt")
        mms_frames = tshark_count(pcap, args.port, "mms")
        pcap_meaningful = tcp_frames >= 20 and tpkt_frames >= 10 and mms_frames >= 10

    wire_trace_summary = validate_wire_trace(wire_trace)
    evidence_mode = "pcap+syscall-wire-trace" if pcap_meaningful else "syscall-wire-trace"

    summary = {
        "schemaVersion": "iedsim-p4-thirdparty-evidence-v2",
        "accepted": True,
        "scope": "third-party-wire-interoperability",
        "externalStack": "MZ Automation libiec61850",
        "externalStackCommit": "664aa00b447292afdf86330745df1b25328aa98f",
        "association": True,
        "discovery": True,
        "read": True,
        "urcbGiInformationReport": True,
        "brcbInformationReport": True,
        "externalMmsWrite": True,
        "controlModels": [1, 2, 3, 4],
        "enhancedCommandTerminationCount": 2,
        "authoritativeStateReadback": True,
        "wireEvidenceMode": evidence_mode,
        "wireTrace": wire_trace_summary,
        "pcap": {
            "path": pcap.name,
            "bytes": pcap.stat().st_size if pcap.is_file() else 0,
            "meaningful": pcap_meaningful,
            "tcpFrames": tcp_frames,
            "tpktFrames": tpkt_frames,
            "mmsFrames": mms_frames,
        },
        "omicronIedScoutAccepted": False,
        "iedscoutInteropPass": False,
    }
    Path(args.output).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(
        "IEDSIM_P4_THIRDPARTY_EVIDENCE_PASS "
        f"mode={evidence_mode} send={wire_trace_summary['sendCalls']} "
        f"recv={wire_trace_summary['recvCalls']} tpkt_syscalls={wire_trace_summary['tpktCalls']} "
        f"pcap_meaningful={str(pcap_meaningful).lower()} "
        "omicron=false IEDSCOUT_INTEROP_PASS=false"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"IEDSIM_P4_THIRDPARTY_EVIDENCE_FAIL {exc}", file=sys.stderr)
        raise
