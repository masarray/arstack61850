#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

DESTINATION_MAC = bytes.fromhex("010ccd040001")
SOURCE_MAC = bytes.fromhex("020000000002")
ETHERTYPE_SV = 0x88BA
APPID = 0x4001
SV_ID = b"ARSTACK61850_P4_TRIAL"
DATASET = b"ARSTACK61850/LLN0$PhsMeas1"
SAMPLE_RATE_HZ = 4_000
SAMPLE_INTERVAL_US = 250
SAMPLE_COUNT_WRAP = 4_000
CHANNEL_COUNT = 8
CAPTURE_START_US = 1_000_000


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def ber_length(length: int) -> bytes:
    require(length >= 0, "negative BER length")
    if length < 0x80:
        return bytes((length,))
    width = max(1, (length.bit_length() + 7) // 8)
    require(width <= 4, "BER length exceeds verifier profile")
    return bytes((0x80 | width,)) + length.to_bytes(width, "big")


def tlv(tag: int, value: bytes) -> bytes:
    return bytes((tag,)) + ber_length(len(value)) + value


def unsigned_value(value: int) -> bytes:
    require(value >= 0, "negative unsigned value")
    width = max(1, (value.bit_length() + 7) // 8)
    return value.to_bytes(width, "big")


def synthetic_payload(sample_count: int) -> bytes:
    payload = bytearray()
    for channel in range(CHANNEL_COUNT):
        value = sample_count * 1_000 + channel * 100_000
        payload.extend(struct.pack(">iI", value, 0))
    return bytes(payload)


def reference_frame(sample_count: int) -> bytes:
    asdu_content = b"".join(
        (
            tlv(0x80, SV_ID),
            tlv(0x81, DATASET),
            tlv(0x82, unsigned_value(sample_count)),
            tlv(0x83, unsigned_value(1)),
            tlv(0x85, unsigned_value(0)),
            tlv(0x86, unsigned_value(SAMPLE_RATE_HZ)),
            tlv(0x87, synthetic_payload(sample_count)),
            tlv(0x88, unsigned_value(1)),
        )
    )
    asdu = tlv(0x30, asdu_content)
    apdu = tlv(0x60, tlv(0x80, unsigned_value(1)) + tlv(0xA2, asdu))
    declared_length = 8 + len(apdu)
    process_bus = struct.pack(">HHHH", APPID, declared_length, 0, 0) + apdu
    return (
        DESTINATION_MAC
        + SOURCE_MAC
        + struct.pack(">H", ETHERTYPE_SV)
        + process_bus
    )


def reference_pcap(frame_count: int) -> bytes:
    output = bytearray(
        struct.pack(
            "<IHHIIII",
            0xA1B2C3D4,
            2,
            4,
            0,
            0,
            65_535,
            1,
        )
    )
    for index in range(frame_count):
        timestamp_us = CAPTURE_START_US + index * SAMPLE_INTERVAL_US
        seconds, microseconds = divmod(timestamp_us, 1_000_000)
        frame = reference_frame(index % SAMPLE_COUNT_WRAP)
        output.extend(struct.pack("<IIII", seconds, microseconds, len(frame), len(frame)))
        output.extend(frame)
    return bytes(output)


def read_tlv(data: bytes, offset: int) -> tuple[int, bytes, int]:
    require(offset + 2 <= len(data), "truncated BER TLV header")
    tag = data[offset]
    first_length = data[offset + 1]
    cursor = offset + 2
    if first_length & 0x80:
        width = first_length & 0x7F
        require(width > 0, "indefinite BER length is not accepted")
        require(width <= 4, "BER length width exceeds verifier profile")
        require(cursor + width <= len(data), "truncated BER long-form length")
        length = int.from_bytes(data[cursor : cursor + width], "big")
        cursor += width
    else:
        length = first_length
    end = cursor + length
    require(end <= len(data), "BER value extends past packet boundary")
    return tag, data[cursor:end], end


def parse_asdu(asdu: bytes, expected_count: int) -> None:
    fields: dict[int, bytes] = {}
    offset = 0
    while offset < len(asdu):
        tag, value, offset = read_tlv(asdu, offset)
        require(tag not in fields, f"duplicate ASDU field tag 0x{tag:02x}")
        fields[tag] = value

    required = {0x80, 0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x88}
    require(required.issubset(fields), "missing required ASDU field")
    require(0x84 not in fields, "refrTm must be absent in first-trial profile")
    require(fields[0x80] == SV_ID, "unexpected svID")
    require(fields[0x81] == DATASET, "unexpected DataSet reference")
    require(int.from_bytes(fields[0x82], "big") == expected_count, "smpCnt mismatch")
    require(int.from_bytes(fields[0x83], "big") == 1, "confRev mismatch")
    require(int.from_bytes(fields[0x85], "big") == 0, "smpSynch mismatch")
    require(int.from_bytes(fields[0x86], "big") == SAMPLE_RATE_HZ, "smpRate mismatch")
    require(int.from_bytes(fields[0x88], "big") == 1, "smpMod mismatch")
    require(fields[0x87] == synthetic_payload(expected_count), "seqData payload mismatch")


def parse_frame(frame: bytes, expected_count: int) -> None:
    require(len(frame) >= 22, "Ethernet frame too short")
    require(frame[:6] == DESTINATION_MAC, "destination MAC mismatch")
    require(frame[6:12] == SOURCE_MAC, "source MAC mismatch")
    ether_type = int.from_bytes(frame[12:14], "big")
    require(ether_type != 0x8100, "VLAN tag present in untagged first-trial profile")
    require(ether_type == ETHERTYPE_SV, "EtherType is not IEC 61850 Sampled Values")

    appid, declared_length, reserved1, reserved2 = struct.unpack(">HHHH", frame[14:22])
    require(appid == APPID, "APPID mismatch")
    require(declared_length == len(frame) - 14, "process-bus declared length mismatch")
    require(reserved1 == 0 and reserved2 == 0, "reserved process-bus fields must be zero")

    tag, apdu_value, end = read_tlv(frame, 22)
    require(tag == 0x60, "SV APDU application tag mismatch")
    require(end == len(frame), "trailing bytes after SV APDU")

    tag, no_asdu, cursor = read_tlv(apdu_value, 0)
    require(tag == 0x80, "missing noASDU field")
    require(int.from_bytes(no_asdu, "big") == 1, "first-trial profile must contain one ASDU")
    tag, sequence, cursor = read_tlv(apdu_value, cursor)
    require(tag == 0xA2, "missing seqASDU field")
    require(cursor == len(apdu_value), "unexpected fields after seqASDU")

    tag, asdu, asdu_end = read_tlv(sequence, 0)
    require(tag == 0x30, "ASDU must use universal SEQUENCE tag")
    require(asdu_end == len(sequence), "first-trial profile must contain exactly one ASDU")
    parse_asdu(asdu, expected_count)


def parse_pcap(raw: bytes) -> list[tuple[int, bytes]]:
    require(len(raw) >= 24, "PCAP global header missing")
    magic = raw[:4]
    if magic == b"\xd4\xc3\xb2\xa1":
        endian = "<"
        nanoseconds = False
    elif magic == b"\xa1\xb2\xc3\xd4":
        endian = ">"
        nanoseconds = False
    elif magic == b"\x4d\x3c\xb2\xa1":
        endian = "<"
        nanoseconds = True
    elif magic == b"\xa1\xb2\x3c\x4d":
        endian = ">"
        nanoseconds = True
    else:
        raise ValueError("unsupported PCAP magic")

    _, major, minor, _, _, snaplen, network = struct.unpack(endian + "IHHIIII", raw[:24])
    require((major, minor) == (2, 4), "unexpected PCAP version")
    require(snaplen >= 1_536, "PCAP snaplen is too small")
    require(network == 1, "PCAP link type is not Ethernet")

    records: list[tuple[int, bytes]] = []
    offset = 24
    while offset < len(raw):
        require(offset + 16 <= len(raw), "truncated PCAP record header")
        seconds, fraction, included, original = struct.unpack(
            endian + "IIII", raw[offset : offset + 16]
        )
        offset += 16
        require(included == original, "truncated PCAP packet is not accepted")
        require(offset + included <= len(raw), "truncated PCAP packet data")
        frame = raw[offset : offset + included]
        offset += included
        if nanoseconds:
            require(fraction % 1_000 == 0, "nanosecond timestamp is not microsecond-aligned")
            fraction //= 1_000
        timestamp_us = seconds * 1_000_000 + fraction
        records.append((timestamp_us, frame))
    return records


def first_difference(left: bytes, right: bytes) -> int | None:
    limit = min(len(left), len(right))
    for index in range(limit):
        if left[index] != right[index]:
            return index
    return None if len(left) == len(right) else limit


def verify(path: Path, expected_frames: int, report_path: Path | None) -> dict[str, object]:
    raw = path.read_bytes()
    expected = reference_pcap(expected_frames)
    if raw != expected:
        mismatch = first_difference(raw, expected)
        raise ValueError(
            "PCAP differs from independent exact-byte reference"
            + ("" if mismatch is None else f" at byte offset {mismatch}")
        )

    records = parse_pcap(raw)
    require(len(records) == expected_frames, "unexpected PCAP frame count")

    previous_timestamp: int | None = None
    first_frame_hash = ""
    last_frame_hash = ""
    for index, (timestamp_us, frame) in enumerate(records):
        expected_timestamp = CAPTURE_START_US + index * SAMPLE_INTERVAL_US
        require(timestamp_us == expected_timestamp, "PCAP timestamp cadence mismatch")
        if previous_timestamp is not None:
            require(timestamp_us - previous_timestamp == SAMPLE_INTERVAL_US, "packet interval mismatch")
        previous_timestamp = timestamp_us

        expected_count = index % SAMPLE_COUNT_WRAP
        parse_frame(frame, expected_count)
        frame_hash = hashlib.sha256(frame).hexdigest()
        if index == 0:
            first_frame_hash = frame_hash
        last_frame_hash = frame_hash

    report: dict[str, object] = {
        "status": "SIMULATED/PASSED",
        "verifier": "independent-python-exact-byte-reference",
        "pcap": str(path),
        "pcap_sha256": hashlib.sha256(raw).hexdigest(),
        "frames": len(records),
        "sample_rate_hz": SAMPLE_RATE_HZ,
        "interval_us": SAMPLE_INTERVAL_US,
        "sample_count_wrap": SAMPLE_COUNT_WRAP,
        "ether_type": f"0x{ETHERTYPE_SV:04x}",
        "appid": f"0x{APPID:04x}",
        "destination_mac": DESTINATION_MAC.hex(":"),
        "source_mac": SOURCE_MAC.hex(":"),
        "sv_id": SV_ID.decode("ascii"),
        "dataset": DATASET.decode("ascii"),
        "payload_bytes": CHANNEL_COUNT * 8,
        "first_frame_sha256": first_frame_hash,
        "last_frame_sha256": last_frame_hash,
    }

    if report_path is not None:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Independently verify the deterministic arstack61850 SV host-simulation PCAP."
    )
    parser.add_argument("pcap", type=Path)
    parser.add_argument("--expected-frames", type=int, default=8_000)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    try:
        require(args.expected_frames > 0, "expected frame count must be positive")
        report = verify(args.pcap, args.expected_frames, args.report)
    except (OSError, ValueError) as exc:
        print(f"SV PCAP QA FAILED: {exc}", file=sys.stderr)
        return 1

    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
