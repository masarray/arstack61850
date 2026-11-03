#!/usr/bin/env python3
"""Synthetic ESP32-P4 merged-image contract failures, no hardware or SDK required."""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from validate_esp32p4_revision_catalog import verify

SOURCE = "0123456789abcdef0123456789abcdef01234567"


def image(minimum, maximum):
    raw = bytearray(bytes([255]) * 0x11000)
    for off in (0x2000, 0x10000):
        raw[off] = 0xE9
        raw[off + 15:off + 17] = minimum.to_bytes(2, "little")
        raw[off + 17:off + 19] = maximum.to_bytes(2, "little")
    raw[0x8000:0x8010] = SOURCE[:16].encode("ascii")
    return bytes(raw)


class CatalogTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.a, self.b = image(100, 299), image(300, 399)
        self.root.joinpath("old.bin").write_bytes(self.a)
        self.root.joinpath("new.bin").write_bytes(self.b)
        a, b = hashlib.sha256(self.a).hexdigest(), hashlib.sha256(self.b).hexdigest()
        self.manifest = dict(chip="esp32p4", version="0.1.1", protocol=1,
                             sourceCommit=SOURCE, image="old.bin", sha256=a)
        self.catalog = dict(schema="arstack.studio.firmware.catalog.v2",
                            chip="esp32p4", version="0.1.1", protocol=1,
                            sourceCommit=SOURCE, releaseChannel="engineering-preview", images=[
                                dict(policy="pre-v3", image="old.bin", sha256=a, minRevision=100, maxRevision=199),
                                dict(policy="v3.x", image="new.bin", sha256=b, minRevision=302, maxRevision=302),
                            ])

    def run_verifier(self):
        self.root.joinpath("firmware-manifest.json").write_text(json.dumps(self.manifest))
        self.root.joinpath("firmware-catalog.json").write_text(json.dumps(self.catalog))
        verify(self.root)

    def reject(self):
        with self.assertRaises(ValueError):
            self.run_verifier()

    def test_valid(self):
        self.run_verifier()

    def test_v3_header_not_legacy(self):
        bad = image(100, 299)
        self.root.joinpath("new.bin").write_bytes(bad)
        self.catalog["images"][1]["sha256"] = hashlib.sha256(bad).hexdigest()
        self.reject()

    def test_legacy_header_never_v3(self):
        bad = image(100, 399)
        self.root.joinpath("old.bin").write_bytes(bad)
        self.manifest["sha256"] = self.catalog["images"][0]["sha256"] = hashlib.sha256(bad).hexdigest()
        self.reject()

    def test_corrupt_image(self):
        self.root.joinpath("new.bin").write_bytes(self.b + b"corrupt")
        self.reject()

    def test_revision_policy_range(self):
        self.catalog["images"][1]["maxRevision"] = 399
        self.reject()

    def test_duplicate_image(self):
        self.catalog["images"][1]["image"] = "old.bin"
        self.reject()

    def test_wrong_provenance(self):
        self.catalog["sourceCommit"] = "f" * 40
        self.reject()

    def test_wrong_application_header(self):
        bad = bytearray(self.b)
        bad[0x10000 + 15:0x10000 + 17] = (100).to_bytes(2, "little")
        self.root.joinpath("new.bin").write_bytes(bad)
        self.catalog["images"][1]["sha256"] = hashlib.sha256(bad).hexdigest()
        self.reject()


if __name__ == "__main__":
    unittest.main(verbosity=2)
