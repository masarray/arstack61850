#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only ESP32-P4 M3E bench evidence gate. Does not flash a device.

Reuse ariec61850_ptp_sv_wire_audit for all IEC 61850/PTP frame decoding.
PCAP host timestamps, firmware telemetry and a claimed GM lock NEVER qualify
absolute physical phase, IED acceptance, or smpSynch=1/2.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from zipfile import ZipFile, BadZipFile

HEX40 = re.compile(r"[a-f0-9]{40}\Z")
HEX64 = re.compile(r"[a-f0-9]{64}\Z")
IDENTITY = re.compile(r"\bARSTACK identity\b[^\r\n]*?\bbuild=([0-9a-fA-F]{16})\b")
PTP2 = re.compile(r"\bPTP2 role=(SOURCE|RECEIVER|MONITOR) discipline=(UNLOCKED|ACQUIRING|LOCKED|HOLDOVER|FAULT) source=(NONE|[0-9A-Fa-f]{16}/[0-9]+)\b")
ROLES = {"source": 0, "receiver": 1}
MAX_ZIP_BYTES = 32 * 1024 * 1024
MAX_SERIAL_LOG_BYTES = 16 * 1024 * 1024
SCENARIOS = ("auto", "receiver-acquisition", "receiver-recovery",
             "receiver-gm-handoff", "source-ready")
BOOT_ID = re.compile(r"\bboot_id=([a-fA-F0-9]{16})\b")
SOURCE_STATUS = re.compile(
    r"\bPTP status=(RUNNING|STOPPED) Announce=([0-9]+) Sync=([0-9]+) "
    r"FollowUp=([0-9]+) PdelayFrames=([0-9]+) TXfail=([0-9]+)\b")
SOURCE_INVALIDATION = re.compile(
    r"\bPTP (?:stop requested|start rejected|status unavailable)\b")
STATUS_FIELD = re.compile(r"\b([A-Za-z][A-Za-z0-9]*)=([^\s]+)")

class EvidenceError(ValueError):
    pass


def require(ok: bool, why: str) -> None:
    if not ok:
        raise EvidenceError(why)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def config_values(data: bytes) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in data.decode("utf-8").splitlines():
        if not line.startswith("CONFIG_") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        require(name not in result, f"duplicate sdkconfig key: {name}")
        result[name] = value.strip().strip('"')
    return result


def verified_bundle(bundle: Path, role: str) -> dict:
    """Validate both roles and their exact flash map; select one only after validation."""
    require(role in ROLES, "invalid role")
    require(bundle.is_file(), "firmware ZIP missing")
    result: dict[str, dict] = {}
    with ZipFile(bundle, "r") as z:
        files = [i for i in z.infolist() if not i.is_dir()]
        require(len(files) <= 32 and len(files) >= 12, "unexpected firmware ZIP entries")
        require(sum(i.file_size for i in files) <= MAX_ZIP_BYTES, "oversized firmware archive")
        names = [i.filename for i in files]
        require(len(names) == len(set(names)), "duplicate ZIP entry")
        for name in names:
            p = PurePosixPath(name)
            require(not p.is_absolute() and ".." not in p.parts and
                    not any(part in ("", ".") for part in p.parts) and
                    len(p.parts) >= 2 and p.parts[0] in ROLES,
                    f"unsafe/unexpected archive path: {name}")
        for name, role_value in ROLES.items():
            prefix = f"{name}/"
            required = {prefix + "manifest.json", prefix + "flasher_args.json", prefix + "sdkconfig"}
            require(required.issubset(names), f"missing {name} bundle metadata")
            manifest = json.loads(z.read(prefix + "manifest.json"))
            flasher = json.loads(z.read(prefix + "flasher_args.json"))
            config_bytes = z.read(prefix + "sdkconfig")
            config = config_values(config_bytes)
            head = manifest.get("prHeadCommit", "")
            require(isinstance(head, str) and HEX40.fullmatch(head) is not None,
                    f"invalid {name} PR commit")
            require(manifest.get("role") == name and manifest.get("boardTarget") == "esp32p4",
                    f"role/target mismatch in {name}")
            require(isinstance(manifest.get("checkoutCommit"), str) and
                    HEX40.fullmatch(manifest["checkoutCommit"]) is not None and
                    "NOT a stable release" in str(manifest.get("kind", "")),
                    f"invalid {name} experiment provenance")
            require(manifest.get("semanticBuildId") == head[:16] and
                    manifest.get("hardwareValidated") is False and
                    manifest.get("smpSynchAutoQualification") is False,
                    f"unsafe {name} qualification/build ID")
            require(manifest.get("sdkconfigSha256") == sha256(config_bytes),
                    f"tampered {name} sdkconfig")
            expected = {
                "CONFIG_IDF_TARGET": "esp32p4",
                "CONFIG_AR_PTP_OPERATING_ROLE": str(role_value),
                "CONFIG_AR_PTP_LAB_TX": "y",
                "CONFIG_AR_SMV_LIVE_TX": "y",
                "CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL": "y",
                "CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL": "y",
                "CONFIG_ETH_TRANSMIT_MUTEX": "y",
            }
            for key, value in expected.items():
                require(config.get(key) == value, f"unsafe {name} config {key}")
            require("CONFIG_AR_SMV_BROADCAST_MIRROR" not in config and
                    "CONFIG_AR_SMV_DIAGNOSTIC_PROBES" not in config,
                    f"experimental mirror/probe enabled in {name}")
            flash_files = flasher.get("flash_files")
            manifest_images = manifest.get("flashImages")
            require(isinstance(flash_files, dict) and len(flash_files) == 3 and
                    isinstance(manifest_images, list) and len(manifest_images) == 3,
                    f"incomplete {name} flash map")
            by_offset = {}
            ranges = []
            app_sha256 = ""
            expected_files = set(required)
            for entry in manifest_images:
                offset = entry.get("flashOffset", "")
                filename = entry.get("relativeFile", "")
                digest = entry.get("sha256", "")
                require(isinstance(offset, str) and offset in flash_files and
                        isinstance(filename, str) and filename.startswith("build/") and
                        isinstance(digest, str) and HEX64.fullmatch(digest) is not None,
                        f"invalid {name} image metadata")
                flash_name = flash_files[offset]
                require(filename == "build/" + flash_name and
                        offset not in by_offset and
                        isinstance(entry.get("bytes"), int),
                        f"{name} flash args/manifest mismatch")
                offset_num = int(offset, 0)
                require(offset_num >= 0x1000 and offset_num % 0x1000 == 0,
                        f"invalid {name} flash offset")
                archive_name = prefix + filename
                payload = z.read(archive_name)
                require(0 < len(payload) <= 8 * 1024 * 1024 and
                        len(payload) == entry["bytes"] and sha256(payload) == digest,
                        f"{name} binary hash/length mismatch at {offset}")
                expected_files.add(archive_name)
                ranges.append((offset_num, offset_num + len(payload)))
                by_offset[offset] = flash_name
                if "/" not in flash_name:
                    app_sha256 = digest
                    require(head[:16].encode("ascii") in payload,
                            f"missing {name} semantic build ID in app binary")
            require(flasher.get("flash_files") == by_offset,
                    f"missing {name} flash image")
            ranges.sort()
            require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])),
                    f"overlapping {name} flash images")
            require(sorted(int(x, 0) for x in by_offset) == [0x2000, 0x8000, 0x10000],
                    f"unrecognized {name} flash layout")
            require(set(n for n in names if n.startswith(prefix)) == expected_files,
                    f"unexpected {name} artifact files")
            result[name] = {"prHeadCommit": head, "role": name,
                            "buildId": head[:16],
                            "offsets": [x for _, x in sorted((int(x, 0), x) for x in by_offset)],
                            "flashImageCount": len(by_offset), "appSha256": app_sha256}
    require(result["source"]["prHeadCommit"] == result["receiver"]["prHeadCommit"],
            "cross-role source commit mismatch")
    require(result["source"]["appSha256"] != result["receiver"]["appSha256"],
            "identical SOURCE/RECEIVER app images (role/config contamination)")
    return {"status": "VERIFIED", "zipSha256": stream_digest(bundle),
            **result[role]}


def serial_evidence(data: bytes, role: str, build_id: str) -> dict:
    lines = data.decode("utf-8", errors="replace").splitlines()
    identities = [m.group(1).lower() for line in lines if (m := IDENTITY.search(line))]
    require(identities, "serial IDENTIFY evidence absent")
    require(all(x == build_id for x in identities), "serial firmware build ID mismatch")
    ptp = [m.groups() for line in lines if (m := PTP2.search(line))]
    require(ptp, "serial PTP SHOW / PTP2 telemetry absent")
    target = role.upper() if role == "source" else "RECEIVER"
    require(all(x[0] == target for x in ptp), "serial PTP role mismatch")
    locked = [x for x in ptp if x[1] == "LOCKED"]
    require(not locked or (role == "receiver" and
            all(x[2] != "NONE" and not x[2].endswith("/0") for x in locked)),
            "false LOCKED without a selected GM or receiver role")
    return {"status": "OBSERVED", "sha256": sha256(data),
            "observations": len(ptp), "states": [x[1] for x in ptp],
            "reportedLockSamples": len(locked),
            "reportedGMPorts": sorted(set(x[2] for x in locked)),
            "sourceSwitchObserved": len(set(x[2] for x in locked)) > 1,
            "note": "Board serial status is not independent GM phase accuracy evidence"}


def operational_evidence(data: bytes, role: str, scenario: str) -> dict:
    """Assess observable *firmware-reported* lifecycle, never PTP accuracy.

    Ordered events are essential: a lock report alone cannot demonstrate
    acquisition; two locks without an intervening demotion cannot prove
    restart/recovery; a new grandmaster must never be treated as continuous
    authority. No second wire protocol decoder or speculative timestamps.
    """
    require(role in ROLES and scenario in SCENARIOS, "unknown bench scenario")
    if scenario == "auto":
        scenario = "receiver-acquisition" if role == "receiver" else "source-ready"
    require((role == "source") == (scenario == "source-ready"),
            "scenario incompatible with firmware role")
    lines = data.decode("utf-8", errors="replace").splitlines()
    identity_lines = [line for line in lines if IDENTITY.search(line)]
    boot_ids = [m.group(1).lower() for line in identity_lines
                if (m := BOOT_ID.search(line))]
    # Every board identity must belong to the same observed boot. A later
    # identity missing boot_id must not silently inherit the previous lease.
    if (not boot_ids or len(boot_ids) != len(identity_lines) or
            len(set(boot_ids)) != 1):
        return {"status": "INCOMPLETE", "scenario": scenario,
                "reason": "one unambiguous 16-digit boot_id required",
                "reportedLockSamples": 0, "reportedRecovery": False,
                "reportedGmHandoff": False}

    if role == "source":
        # A historical RUNNING line is not current readiness. The actual
        # firmware SHOW includes TXfail and PdelayFrames; partial output is
        # never acceptable. STOP, rejected START and unavailable status
        # invalidate all prior ready observations.
        last_ready = False
        tx_fault_observed = False
        ready_samples = 0
        for line in lines:
            if SOURCE_INVALIDATION.search(line):
                last_ready = False
            if "PTP status=" not in line:
                continue
            match = SOURCE_STATUS.search(line)
            if match is None:
                last_ready = False
                continue
            state, *reported = match.groups()
            announce, sync, followup, _pdelay, txfail = map(int, reported)
            tx_fault_observed = tx_fault_observed or txfail != 0
            last_ready = (state == "RUNNING" and
                          announce > 0 and sync > 0 and followup > 0 and
                          txfail == 0)
            ready_samples += int(last_ready)
        usable = last_ready and not tx_fault_observed
        return {"status": "OBSERVED" if usable else "INCOMPLETE",
                "scenario": scenario,
                "reason": "latest SOURCE status RUNNING with successful PTP TX"
                          if usable else "latest SOURCE readiness absent, stopped or TX faulted",
                "reportedSourceReadySamples": ready_samples,
                "sourceTxFaultObserved": tx_fault_observed,
                "reportedLockSamples": 0,
                "reportedRecovery": False, "reportedGmHandoff": False}

    snapshots = []
    invalid_locks = 0
    for line in lines:
        m = PTP2.search(line)
        if not m:
            continue
        role_text, state, gm = m.groups()
        require(role_text == "RECEIVER", "receiver lifecycle contains a different role")
        fields = dict(STATUS_FIELD.findall(line[m.start():]))
        valid_lock = False
        if state == "LOCKED":
            # Actual firmware emits all of these on each PTP2 SHOW. A
            # truncated/handwritten line must never satisfy the scenario.
            numeric = ("offset", "path", "jitter", "rxSync", "rxFollowUp",
                       "rxPdelay", "accepted")
            try:
                measurements = {k: int(fields[k], 10) for k in numeric}
                gm_clock, gm_port = gm.split("/")
                valid_lock = (gm_clock != "0" * 16 and int(gm_port) > 0 and
                              measurements["rxSync"] > 0 and
                              measurements["rxFollowUp"] > 0 and
                              measurements["rxPdelay"] > 0 and
                              measurements["accepted"] > 0 and
                              measurements["path"] >= 0 and
                              measurements["jitter"] >= 0)
            except (KeyError, ValueError):
                valid_lock = False
        if state == "LOCKED" and not valid_lock:
            invalid_locks += 1
        snapshots.append((state, gm.upper(), valid_lock))

    if not snapshots:
        return {"status": "INCOMPLETE", "scenario": scenario,
                "reason": "receiver PTP2 lifecycle missing", "reportedLockSamples": 0,
                "reportedRecovery": False, "reportedGmHandoff": False}
    # Require evidence of a prior non-LOCKED state for cold acquisition.
    # For recovery/switch, a complete measured LOCKED snapshot must occur on
    # BOTH sides of a demotion. A direct locked->locked GM swap is invalid.
    acquired = False
    saw_acquisition = False
    recovery = False
    handoff = False
    unsafe_direct_handoff = False
    previous_gm = None
    demoted = False
    # One O(n) pass over bounded serial events, never O(n^2) prefixes.
    for state, gm, valid in snapshots:
        if state != "LOCKED":
            if state in ("UNLOCKED", "ACQUIRING"):
                saw_acquisition = True
            if previous_gm is not None:
                demoted = True
        elif valid:
            if saw_acquisition:
                acquired = True
            if previous_gm is not None:
                if demoted:
                    recovery = True
                    if gm != previous_gm:
                        handoff = True
                elif gm != previous_gm:
                    unsafe_direct_handoff = True
            previous_gm = gm
            demoted = False
    satisfied = ({"receiver-acquisition": acquired,
                  "receiver-recovery": recovery,
                  "receiver-gm-handoff": handoff}[scenario] and
                 invalid_locks == 0 and not unsafe_direct_handoff)
    reasons = {
        "receiver-acquisition": "UNLOCKED/ACQUIRING then measured LOCKED",
        "receiver-recovery": "LOCKED, observed demotion, measured LOCKED reacquisition",
        "receiver-gm-handoff": "LOCKED old GM, demotion, LOCKED different GM",
    }
    return {"status": "OBSERVED" if satisfied else "INCOMPLETE",
            "scenario": scenario,
            "reason": reasons[scenario] if satisfied else
                      "required ordered lifecycle evidence not observed",
            "reportedLockSamples": sum(s == "LOCKED" and v
                                       for s, _, v in snapshots),
            "reportedRecovery": recovery, "reportedGmHandoff": handoff,
            "invalidLockReports": invalid_locks,
            "directGmHandoffWithoutDemotion": unsafe_direct_handoff,
            "observedStateSequence": [s for i, (s, _, _) in enumerate(snapshots)
                                      if i == 0 or s != snapshots[i - 1][0]][:64],
            "note": "Firmware PTP state is not externally measured GM/phase qualification"}


def wire_evidence(audit: dict) -> dict:
    require(audit.get("evidence") == "wire_only_unqualified", "not an ARIEC61850 wire audit")
    require(all(audit.get(x) is None for x in (
        "ptpClockSynchronized", "svPtpPhaseQualified", "iedAlarmFree")),
        "wire parser attempted to assert unmeasured hardware qualification")
    fields = ("svAsdus", "smpSynch0", "smpSynch1", "smpSynch2", "smpSynchUnknown",
              "gaps", "duplicates", "outOfOrder", "configurationChanges",
              "counterZeroCaptureObservations", "normalWraps")
    require(all(type(audit.get(x)) is int and audit[x] >= 0 for x in fields),
            "wire evidence missing/invalid numerical counters")
    require(audit["svAsdus"] == sum(audit[x] for x in
            ("smpSynch0", "smpSynch1", "smpSynch2", "smpSynchUnknown")),
            "SV ASDU sync accounting mismatch")
    consistent = (audit.get("svWireConsistent") is True and
                  audit.get("ptpPacketsVisible") is True and
                  audit["smpSynch0"] == audit["svAsdus"] and
                  audit["svAsdus"] > 0 and
                  audit["gaps"] == audit["duplicates"] == audit["outOfOrder"] == 0 and
                  audit["configurationChanges"] == 0 and
                  audit["counterZeroCaptureObservations"] >= 2 and
                  audit["normalWraps"] >= 1)
    return {"status": "CONSISTENT" if consistent else "REJECTED",
            "continuousEpochsObserved": audit["counterZeroCaptureObservations"],
            "normalCounterWraps": audit["normalWraps"],
            "selectedSvAsdus": audit["svAsdus"],
            "allWireSmpSynchFailClosed": audit["smpSynch0"] == audit["svAsdus"]}


def run_wire_audit(binary: Path, pcap: Path, sv_id: str, app_id: int,
                   modulus: int, domain: int) -> dict:
    require(binary.is_file() and pcap.is_file(), "PCAP or host wire audit executable missing")
    args = [str(binary.resolve()), str(pcap.resolve()), "--sv-id", sv_id,
            "--app-id", hex(app_id), "--counter-modulus", str(modulus),
            "--ptp-domain", str(domain), "--json"]
    # The native C++ auditor reads PCAP incrementally, not into unbounded RAM.
    proc = subprocess.run(args, capture_output=True, text=True,
                          timeout=180, check=False)
    require(proc.returncode in (0, 2), "ARIEC61850 wire audit failed to parse PCAP")
    audit = json.loads(proc.stdout)
    result = wire_evidence(audit)
    require((proc.returncode == 0) == (audit.get("svWireConsistent") is True and
            audit.get("ptpPacketsVisible") is True),
            "inconsistent native wire audit exit status")
    result["pcapSha256"] = stream_digest(pcap)
    return result


def stream_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--role", choices=tuple(ROLES), required=True)
    parser.add_argument("--serial-log", type=Path)
    parser.add_argument("--scenario", choices=SCENARIOS, default="auto",
                        help="required ordered firmware-reported lifecycle for full evidence")
    parser.add_argument("--pcap", type=Path)
    parser.add_argument("--wire-audit", type=Path,
                        help="built ariec61850_ptp_sv_wire_audit host binary")
    parser.add_argument("--sv-id")
    parser.add_argument("--app-id", type=lambda s: int(s, 0))
    parser.add_argument("--counter-modulus", type=int)
    parser.add_argument("--ptp-domain", type=int)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        require((args.pcap is None) == (args.wire_audit is None),
                "PCAP requires --wire-audit, and vice versa")
        firmware = verified_bundle(args.bundle, args.role)
        require(args.scenario == "auto" or
                ((args.role == "source") == (args.scenario == "source-ready")),
                "scenario incompatible with firmware role")
        serial = {"status": "MISSING"}
        lifecycle = {"status": "MISSING", "scenario": args.scenario}
        if args.serial_log:
            require(args.serial_log.stat().st_size <= MAX_SERIAL_LOG_BYTES,
                    "serial evidence too large")
            serial_bytes = args.serial_log.read_bytes()
            serial = serial_evidence(serial_bytes, args.role, firmware["buildId"])
            lifecycle = operational_evidence(serial_bytes, args.role, args.scenario)
        wire = {"status": "MISSING"}
        if args.pcap:
            require(args.sv_id is not None and args.app_id is not None and
                    args.counter_modulus is not None and args.ptp_domain is not None and
                    0 <= args.app_id <= 65535 and args.counter_modulus >= 2 and
                    0 <= args.ptp_domain <= 255,
                    "PCAP requires bounded --sv-id, --app-id, --counter-modulus, --ptp-domain")
            wire = run_wire_audit(args.wire_audit, args.pcap, args.sv_id,
                                  args.app_id, args.counter_modulus, args.ptp_domain)
        report = {
            "schema": "arstack-ptp-m3e-bench-gate-v1",
            "firmware": firmware,
            "serial": serial,
            "operationalLifecycle": lifecycle,
            "wire": wire,
            "softwareEvidenceConsistent": wire["status"] == "CONSISTENT" and
                                          serial["status"] == "OBSERVED" and
                                          lifecycle["status"] == "OBSERVED",
            "independentHardwarePhaseQualified": None,
            "independentPTPGrandmasterAccuracyQualified": None,
            "relayAcceptanceQualified": None,
            "autoSmpSynchPromotionPermitted": False,
            "publicReleasePermitted": False,
        }
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(payload, encoding="utf-8")
        sys.stdout.write(payload)
        return 0 if (not args.serial_log and not args.pcap) or report["softwareEvidenceConsistent"] else 2
    except (EvidenceError, OSError, ValueError, KeyError, BadZipFile,
            subprocess.TimeoutExpired, RuntimeError) as exc:
        print(f"PTP M3E bench evidence REJECTED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())