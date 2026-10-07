#!/usr/bin/env python3
"""Regression for C++-owned host profile bytes and GUI binary handoff."""
import json
import pathlib
import subprocess
import sys
import zlib

root = pathlib.Path(__file__).resolve().parents[2]
tool = pathlib.Path(sys.argv[1]).resolve()
fixture = root / "tests/fixtures/scl/sv-4800-structured-4i4v.scd"
cmd = [str(tool), str(fixture), "--stream-profile-family", "0:iec61850-9-2",
       "--counter-modulus", "4800"]
run = subprocess.run(cmd, text=True, capture_output=True, timeout=20, check=True)
model = json.loads(run.stdout)
stream = model["streams"][0]
assert stream["compatibilityClass"] == "A", stream
assert stream["deviceSupport"] == "ready", stream
hex_text = stream.get("deviceProfileHex")
assert isinstance(hex_text, str) and hex_text, stream
record = bytes.fromhex(hex_text)
assert record[:4] == b"ARSV", record[:4]
assert int.from_bytes(record[4:6], "big") == 1
assert int.from_bytes(record[8:12], "big") == len(record)
wire_crc = int.from_bytes(record[16:20], "big")
without_crc = record[:16] + bytes(4) + record[20:]
assert zlib.crc32(without_crc) == wire_crc
assert len(record) <= 572
print("C++ SCL -> canonical device profile binary V1: PASS", len(record), "bytes")
