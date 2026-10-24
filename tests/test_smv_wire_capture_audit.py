#!/usr/bin/env python3
"""Synthetic decoder tests: never presented as hardware acceptance evidence."""
import importlib.util
import math
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("wire_audit", ROOT / "tools/smv_wire_capture_audit.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def tlv(tag, value):
    n = len(value)
    size = bytes([n]) if n < 128 else bytes([0x82]) + n.to_bytes(2, "big")
    return bytes([tag]) + size + value

def packet(seq=0, change=0, quality=0, samples=None):
    values = samples if samples is not None else [1000 * i + change for i in range(8)]
    payload = b"".join(struct.pack(">iI", value, quality) for value in values)
    asdu = tlv(0x30,
               tlv(0x80, b"ARSTACK_SV01") +
               tlv(0x82, seq.to_bytes(2, "big")) +
               tlv(0x83, b"\x01") +
               tlv(0x85, b"\x01") +
               tlv(0x87, payload))
    pdu = tlv(0x60, tlv(0x80, b"\x01") + tlv(0xa2, asdu))
    eth = bytes.fromhex("011ccd04000102000000000188ba")
    return eth + struct.pack(">HHHH", 0x4000, len(pdu) + 8, 0, 0) + pdu

def write_pcap(path, records):
    with path.open("wb") as file:
        file.write(bytes.fromhex("d4c3b2a1") + struct.pack("<HHIIII", 2, 4, 0, 0, 65535, 1))
        for stamp, frame in records:
            sec = int(stamp)
            micros = int(round((stamp - sec) * 1000000))
            file.write(struct.pack("<IIII", sec, micros, len(frame), len(frame)))
            file.write(frame)

class WireAuditTests(unittest.TestCase):
    def test_decode_eight_channels_and_quality(self):
        parsed = module.decode_frame(packet(42, quality=0x12345678))
        self.assertEqual(parsed["count"], 42)
        self.assertEqual(parsed["sv_id"], "ARSTACK_SV01")
        self.assertEqual(len(parsed["channels"]), 8)
        self.assertEqual(parsed["channels"][7], (7000, 0x12345678))
        self.assertEqual(parsed["synch"], 1)

    def test_continuous_stream_and_wrap(self):
        with tempfile.TemporaryDirectory() as directory:
            name = Path(directory) / "ok.pcap"
            frames = [(10 + i / 4000, packet((3998 + i) % 4000)) for i in range(12)]
            write_pcap(name, frames)
            report = module.analyze(name, 0x4000, "ARSTACK_SV01", 4000, .002, 4000)
            self.assertEqual(report["status"], "PASS_PCAPPARSER_ONLY")
            self.assertEqual(report["missing_samples"], 0)
            self.assertEqual(report["duplicate_samples"], 0)
            self.assertEqual(report["matched_sv_frames"], 12)

    def test_drop_and_duplicate_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            name = Path(directory) / "broken.pcap"
            write_pcap(name, [(1, packet(0)), (1.00025, packet(2)),
                              (1.0005, packet(2))])
            report = module.analyze(name, 0x4000, "ARSTACK_SV01", 4000, .0005, 4000)
            self.assertEqual(report["status"], "FAIL")
            self.assertGreater(report["missing_samples"], 0)
            self.assertGreater(report["duplicate_samples"], 0)

    def test_signed_phase_and_rms_independent_equivalence(self):
        with tempfile.TemporaryDirectory() as directory:
            name = Path(directory) / "wave.pcap"
            records = []
            for i in range(400):
                theta = 2 * math.pi * 50 * i / 4000
                a = round(1000 * math.sqrt(2) * math.sin(theta - math.pi / 6))
                b = round(2000 * math.sqrt(2) * math.sin(theta - 5 * math.pi / 6))
                records.append((10 + i / 4000, packet(i, samples=[a, b, 0, 0, 0, 0, 0, 0])))
            write_pcap(name, records)
            intended = {name: {"rms_counts": 0, "phase_degrees": 0}
                        for name in module.NAMES}
            intended["IA"] = {"rms_counts": 1000, "phase_degrees": -30}
            intended["IB"] = {"rms_counts": 2000, "phase_degrees": -150}
            report = module.analyze(name, 0x4000, "ARSTACK_SV01", 4000,
                                    .09, 4000, 50.0, intended)
            self.assertEqual(report["status"], "PASS_PCAPPARSER_ONLY", report["blocking_findings"])
            intended["IA"]["quality"] = "0x01"
            wrong_quality = module.analyze(name, 0x4000, "ARSTACK_SV01", 4000,
                                           .09, 4000, 50.0, intended)
            self.assertEqual(wrong_quality["status"], "FAIL")
            self.assertTrue(any("IA: per-sample quality" in err
                                for err in wrong_quality["blocking_findings"]))
            intended["IA"].pop("quality")
            intended["IB"]["phase_degrees"] = 120
            mismatch = module.analyze(name, 0x4000, "ARSTACK_SV01", 4000,
                                      .09, 4000, 50.0, intended)
            self.assertEqual(mismatch["status"], "FAIL")
            self.assertTrue(any("IB: fundamental phase" in err
                                for err in mismatch["blocking_findings"]))

    def test_truncated_frame_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            name = Path(directory) / "bad.pcap"
            write_pcap(name, [(1, packet(0)[:-20])])
            report = module.analyze(name, 0x4000, "ARSTACK_SV01", 4000, .001, 4000)
            self.assertEqual(report["status"], "FAIL")
            self.assertEqual(report["malformed_sv_frames"], 1)

if __name__ == "__main__":
    unittest.main()
