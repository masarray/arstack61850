#!/usr/bin/env python3
"""Independent, read-only IEC 61850-9-2LE on-wire acceptance from classic PCAP.

No Studio telemetry, mock success path, or third-party decoder is trusted.
Physical 4I+4V acceptance is *not* inferred from synthetic fixtures.
"""
import argparse
import collections
import json
import math
import struct
import sys
from pathlib import Path

NAMES = ("IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN")

def tlvs(data):
    offset = 0
    while offset < len(data):
        if offset + 2 > len(data):
            raise ValueError("truncated BER tag")
        tag, n = data[offset], data[offset + 1]
        offset += 2
        if n & 0x80:
            count = n & 0x7f
            if count == 0 or count > 3 or offset + count > len(data):
                raise ValueError("invalid BER length")
            n = int.from_bytes(data[offset:offset + count], "big")
            offset += count
        if offset + n > len(data):
            raise ValueError("BER value exceeds SV APDU")
        yield tag, data[offset:offset + n]
        offset += n

def decode_frame(frame):
    if len(frame) < 22:
        return None
    off = 12
    ethertype = int.from_bytes(frame[off:off + 2], "big")
    for _ in range(2):
        if ethertype not in (0x8100, 0x88a8):
            break
        off += 4
        if off + 2 > len(frame):
            raise ValueError("truncated VLAN")
        ethertype = int.from_bytes(frame[off:off + 2], "big")
    if ethertype != 0x88ba:
        return None
    if len(frame) < off + 10:
        raise ValueError("truncated SV header")
    appid, length = struct.unpack_from(">HH", frame, off + 2)
    if length < 8 or off + 2 + length > len(frame):
        raise ValueError("bad SV declared length")
    apdu = frame[off + 10:off + 2 + length]
    outer = list(tlvs(apdu))
    if len(outer) != 1 or outer[0][0] != 0x60:
        raise ValueError("SV application APDU missing")
    items = dict(tlvs(outer[0][1]))
    seq = items.get(0xa2)
    if seq is None:
        raise ValueError("SV ASDU sequence missing")
    records = []
    for tag, asdu in tlvs(seq):
        if tag != 0x30:
            raise ValueError("unexpected ASDU tag")
        fields = dict(tlvs(asdu))
        if 0x82 not in fields or 0x87 not in fields or 0x80 not in fields:
            raise ValueError("missing smpCnt, seqData or svID")
        payload = fields[0x87]
        if len(payload) != 64:
            raise ValueError(f"expected 8 x 8 bytes of seqData, got {len(payload)}")
        values = [struct.unpack_from(">iI", payload, i * 8) for i in range(8)]
        records.append({"appid": appid, "sv_id": fields[0x80].decode("ascii"),
                        "count": int.from_bytes(fields[0x82], "big"),
                        "synch": int.from_bytes(fields.get(0x85, b"\x00"), "big"),
                        "channels": values})
    if len(records) != 1:
        raise ValueError("9-2LE acceptance expects exactly one ASDU per SV Ethernet frame")
    return records[0]

def frames(path):
    with path.open("rb") as f:
        header = f.read(24)
        if len(header) != 24:
            raise ValueError("truncated PCAP global header")
        magic = header[:4]
        if magic == bytes.fromhex("d4c3b2a1"):
            endian, resolution = "<", 1000000
        elif magic == bytes.fromhex("a1b2c3d4"):
            endian, resolution = ">", 1000000
        elif magic == bytes.fromhex("4d3cb2a1"):
            endian, resolution = "<", 1000000000
        elif magic == bytes.fromhex("a1b23c4d"):
            endian, resolution = ">", 1000000000
        else:
            raise ValueError("unsupported capture format (classic Ethernet PCAP required)")
        _, _, _, _, _, linktype = struct.unpack(endian + "HHIIII", header[4:])
        if linktype != 1:
            raise ValueError("only Ethernet linktype=1 is accepted")
        while True:
            record = f.read(16)
            if not record:
                break
            if len(record) != 16:
                raise ValueError("truncated PCAP packet header")
            seconds, sub, incl, original = struct.unpack(endian + "IIII", record)
            if incl > 1048576 or incl > original:
                raise ValueError("invalid captured packet size")
            data = f.read(incl)
            if len(data) != incl:
                raise ValueError("truncated PCAP Ethernet packet")
            yield seconds + sub / resolution, data

def analyze(path, expected_appid, expected_svid, modulus, min_seconds, expected_fps,
            signal_frequency_hz=50.0, expected_channels=None):
    counts = collections.Counter()
    previous = None
    first_ts = last_ts = None
    # Constant-space streaming aggregates: 14.4 million frames/hour at 4 kfps.
    minimums = [None] * 8
    maximums = [None] * 8
    squared_sums = [0] * 8
    sin_sums = [0.0] * 8
    cos_sums = [0.0] * 8
    sample_counts = [0] * 8
    quality = [collections.Counter() for _ in range(8)]
    sources = collections.Counter()
    gaps = duplicates = reversals = malformed = 0
    for stamp, raw in frames(path):
        try:
            record = decode_frame(raw)
        except (ValueError, UnicodeError):
            malformed += 1
            continue
        if record is None:
            continue
        counts["SV"] += 1
        sources[(record["appid"], record["sv_id"])] += 1
        if record["appid"] != expected_appid or record["sv_id"] != expected_svid:
            counts["foreign"] += 1
            continue
        counts["matched"] += 1
        if first_ts is None:
            first_ts = stamp
        last_ts = stamp
        current = record["count"]
        if current >= modulus:
            counts["out_of_range_count"] += 1
        if previous is not None:
            distance = (current - previous) % modulus
            if distance == 0:
                duplicates += 1
            elif distance != 1:
                gaps += distance - 1
                if current < previous and previous != modulus - 1:
                    reversals += 1
        previous = current
        counts["smpSynch_" + str(record["synch"])] += 1
        theta = 2.0 * math.pi * signal_frequency_hz * current / expected_fps
        sin_theta, cos_theta = math.sin(theta), math.cos(theta)
        for i, (value, q) in enumerate(record["channels"]):
            minimums[i] = value if minimums[i] is None else min(minimums[i], value)
            maximums[i] = value if maximums[i] is None else max(maximums[i], value)
            squared_sums[i] += value * value
            sin_sums[i] += value * sin_theta
            cos_sums[i] += value * cos_theta
            sample_counts[i] += 1
            quality[i][q] += 1
    duration = 0 if first_ts is None else last_ts - first_ts
    rate = (counts["matched"] - 1) / duration if duration > 0 else 0
    fundamental = []
    for i in range(8):
        n = sample_counts[i]
        if not n:
            fundamental.append({"rms": None, "phase_degrees": None})
            continue
        a = sin_sums[i] * 2.0 / n
        b = cos_sums[i] * 2.0 / n
        fundamental.append({
            "rms": round(math.hypot(a, b) / math.sqrt(2.0), 3),
            "phase_degrees": round(math.degrees(math.atan2(b, a)), 3)
        })
    report = {
        "source_file": str(path), "status": "FAIL", "matched_sv_frames": counts["matched"],
        "total_sv_frames": counts["SV"], "foreign_sv_frames": counts["foreign"],
        "malformed_sv_frames": malformed, "duration_seconds": round(duration, 6),
        "observed_fps": round(rate, 3), "expected_fps": expected_fps,
        "smpCnt_modulus": modulus, "missing_samples": gaps,
        "duplicate_samples": duplicates, "non_boundary_wraps": reversals,
        "out_of_range_sample_counts": counts["out_of_range_count"],
        "smpSynch_counts": {key: value for key, value in counts.items()
                           if key.startswith("smpSynch_")},
        "streams_seen": [{"appid": appid, "svID": svid, "frames": n}
                         for (appid, svid), n in sorted(sources.items())],
        "channels": [
            {"name": NAMES[i], "min": minimums[i],
             "max": maximums[i],
             "rms_wire_counts": round((squared_sums[i] / sample_counts[i]) ** 0.5, 3)
             if sample_counts[i] else None,
             "fundamental_rms_wire_counts": fundamental[i]["rms"],
             "fundamental_phase_degrees": fundamental[i]["phase_degrees"],
             "qualities": {f"0x{q:08X}": n for q, n in quality[i].items()}}
            for i in range(8)],
    }
    issues = []
    if duration < min_seconds:
        issues.append("capture shorter than required evidence duration")
    if counts["matched"] < 2:
        issues.append("insufficient matching SV frames")
    if counts["foreign"] or malformed:
        issues.append("foreign or malformed SV frames present")
    if gaps or duplicates or reversals or counts["out_of_range_count"]:
        issues.append("smpCnt sequence discontinuity")
    if rate < expected_fps * .98 or rate > expected_fps * 1.02:
        issues.append("average frame rate outside ±2%")
    if sample_counts[0] < 2 or any(n == 0 for n in sample_counts):
        issues.append("incomplete 8-channel wire payload")
    if expected_channels is not None:
        for i, name in enumerate(NAMES):
            expected = expected_channels[name]
            if "quality" in expected:
                wanted = expected["quality"]
                wanted_q = int(wanted, 0) if isinstance(wanted, str) else int(wanted)
                if quality[i] != {wanted_q: sample_counts[i]}:
                    issues.append(f"{name}: per-sample quality differs from operator scenario")
            intended_rms = float(expected["rms_counts"])
            intended_phase = float(expected["phase_degrees"])
            tolerance = float(expected.get("rms_tolerance_percent", 2.0))
            phase_tolerance = float(expected.get("phase_tolerance_degrees", 2.0))
            observed = fundamental[i]
            if intended_rms == 0.0:
                if observed["rms"] is None or observed["rms"] > float(expected.get("zero_tolerance_counts", 2.0)):
                    issues.append(f"{name}: zero-valued channel has residual fundamental")
            else:
                if (observed["rms"] is None or
                    abs(observed["rms"] - intended_rms) > abs(intended_rms) * tolerance / 100):
                    issues.append(f"{name}: fundamental RMS differs from operator scenario")
                if observed["phase_degrees"] is None:
                    issues.append(f"{name}: fundamental phase unavailable")
                else:
                    delta = (observed["phase_degrees"] - intended_phase + 180) % 360 - 180
                    if abs(delta) > phase_tolerance:
                        issues.append(f"{name}: fundamental phase differs from operator scenario")
    report["blocking_findings"] = issues
    report["status"] = "PASS_PCAPPARSER_ONLY" if not issues else "FAIL"
    report["disclaimer"] = (
        "Fundamental waveform comparisons are only authoritative with --expected-json "
        "containing independent operator raw-wire setpoints. PTP lock, smpSynch legitimacy, "
        "source MAC, realtime CPU and hardware operation need separate evidence.")
    return report

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("pcap", type=Path)
    ap.add_argument("--appid", type=lambda s: int(s, 0), default=0x4000)
    ap.add_argument("--sv-id", default="ARSTACK_SV01")
    ap.add_argument("--wrap", type=int, default=4000)
    ap.add_argument("--minimum-seconds", type=float, default=120.0)
    ap.add_argument("--fps", type=float, default=4000.0)
    ap.add_argument("--signal-frequency-hz", type=float, default=50.0)
    ap.add_argument("--expected-json", type=Path,
                    help="Scenario JSON containing channels IA..UN with rms_counts and phase_degrees")
    args = ap.parse_args()
    try:
        if args.wrap <= 1 or args.fps <= 0 or args.minimum_seconds <= 0 or args.signal_frequency_hz <= 0:
            raise ValueError("invalid acceptance criteria")
        expected = None
        if args.expected_json is not None:
            config = json.loads(args.expected_json.read_text(encoding="utf-8"))
            expected = config["channels"]
            if set(expected) != set(NAMES):
                raise ValueError("expected scenario must specify exactly IA IB IC IN UA UB UC UN")
            if config.get("ct_shaping_enabled", False):
                raise ValueError("fundamental equivalence with nonlinear CT shaping is not supported")
        report = analyze(args.pcap, args.appid, args.sv_id, args.wrap,
                         args.minimum_seconds, args.fps, args.signal_frequency_hz, expected)
        print(json.dumps(report, indent=2))
        return 0 if report["status"] == "PASS_PCAPPARSER_ONLY" else 2
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(json.dumps({"status": "ERROR", "reason": str(exc)}), file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
