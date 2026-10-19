#!/usr/bin/env python3
"""Synthetic decoder tests: never presented as hardware acceptance evidence."""
import importlib.util
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

def packet(seq=0, change=0, quality=0):
    payload = b"".join(struct.pack(">iI", 1000 * i + change, quality) for i in range(8))
    asdu = tlv(0x30,
               tlv(0x80, b"ARSTACK_SV01") +
               tlv(0x82, seq.to_bytes(2, "big")) +
               tlv(0x83, b"\\x01") +
               tlv(0x85, b"\\x01") +
               tlv(0x87, payload))
    pdu = tlv(0x60, tlv(0x80, b"\\x01") + tlv(0xa2, asdu))
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

    def test_truncated_frame_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            name = Path(directory) / "bad.pcap"
            write_pcap(name, [(1, packet(0)[:-20])])
            report = module.analyze(name, 0x4000, "ARSTACK_SV01", 4000, .001, 4000)
            self.assertEqual(report["status"], "FAIL")
            self.assertEqual(report["malformed_sv_frames"], 1)

if __name__ == "__main__":
    unittest.main()
