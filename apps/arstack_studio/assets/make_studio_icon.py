#!/usr/bin/env python3
"""Deterministic Windows ARStack Studio icon, built with Python stdlib only.

A single generated ICO is embedded in the PE executable and reused by Inno
Setup, Desktop and Start menu shortcuts. No image/font build dependencies.
"""
import math
import pathlib
import struct
import sys
import zlib


def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(
        ">I", zlib.crc32(tag + body) & 0xffffffff
    )


def make_png(size):
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            px = (x + 0.5) / size
            py = (y + 0.5) / size
            inside = (0.05 <= px <= 0.95 and 0.05 <= py <= 0.95)
            corner = (min(abs(px - 0.19), abs(px - 0.81)) ** 2
                      + min(abs(py - 0.19), abs(py - 0.81)) ** 2) < 0.0225
            alpha = 255 if inside or corner else 0
            col = (12, 29, 53)
            if alpha and (px < 0.075 or px > 0.925 or py < 0.075 or py > 0.925):
                col = (29, 115, 193)
            # Bright dual-wave symbol: PTP timing and 4I+4V SV.
            for offset, color in ((-0.095, (80, 184, 255)), (0.10, (46, 221, 197))):
                curve = 0.57 + offset + 0.13 * math.sin((px - 0.18) * math.pi * 3.1)
                if 0.14 <= px <= 0.86 and abs(py - curve) < 0.028:
                    col = color
            # Three timing/sampling reference dots.
            if any((px - cx) ** 2 + (py - 0.23) ** 2 < 0.0022
                   for cx in (0.28, 0.50, 0.72)):
                col = (242, 249, 255)
            rows.extend((*col, alpha))
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\\x89PNG\\r\\n\\x1a\\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
            + chunk(b"IEND", b""))


def main():
    output = pathlib.Path(sys.argv[1])
    sizes = (16, 24, 32, 48, 64, 128)
    images = [make_png(n) for n in sizes]
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in zip(sizes, images):
        directory.extend(struct.pack("<BBBBHHII", size if size < 256 else 0,
                                     size if size < 256 else 0, 0, 0, 1, 32,
                                     len(data), offset))
        offset += len(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bytes(directory) + b"".join(images))
    print(f"ARStack Studio Windows icon: {output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
