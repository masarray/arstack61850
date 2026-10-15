#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""M3E bounded ZIP, firmware identity, serial/PTP and wire evidence regressions."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import warnings
import zipfile

spec = importlib.util.spec_from_file_location(
    "m3e", Path(__file__).resolve().parents[1] / "scripts" /
    "ptp_m3e_bench_gate.py")
m3e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m3e)
SHA = "7d2d5146b1892124c1744ad26ab31b3489e8387e"


def make_zip(change=None):
    files = {}
    for role, n in (("source", 0), ("receiver", 1)):
        config = (
            'CONFIG_IDF_TARGET="esp32p4"\n'
            f'CONFIG_AR_PTP_OPERATING_ROLE={n}\n'
            'CONFIG_AR_PTP_LAB_TX=y\nCONFIG_AR_SMV_LIVE_TX=y\n'
            'CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL=y\n'
            'CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL=y\n'
            'CONFIG_ETH_TRANSMIT_MUTEX=y\n'
            '# CONFIG_AR_SMV_BROADCAST_MIRROR is not set\n'
            '# CONFIG_AR_SMV_DIAGNOSTIC_PROBES is not set\n').encode()
        mapping = {
            "0x2000": "bootloader/bootloader.bin",
            "0x8000": "partition_table/partition-table.bin",
            "0x10000": "arstack_esp32p4_smv_injector.bin",
        }
        imgs = []
        for off, name in mapping.items():
            b = (SHA[:16] + role).encode() if "/" not in name else name.encode()
            files[f"{role}/build/{name}"] = b
            imgs.append({"flashOffset": off, "relativeFile": "build/" + name,
                         "bytes": len(b), "sha256": m3e.sha256(b)})
        meta = {
            "kind": "ARStack61850 experimental PTP bench-only; NOT a stable release",
            "boardTarget": "esp32p4", "role": role, "prHeadCommit": SHA,
            "checkoutCommit": "c" * 40, "semanticBuildId": SHA[:16],
            "hardwareValidated": False, "smpSynchAutoQualification": False,
            "sdkconfigSha256": m3e.sha256(config), "flashImages": imgs,
        }
        files[f"{role}/manifest.json"] = json.dumps(meta).encode()
        files[f"{role}/flasher_args.json"] = json.dumps({"flash_files": mapping}).encode()
        files[f"{role}/sdkconfig"] = config
    if change:
        change(files)
    memory = io.BytesIO()
    with zipfile.ZipFile(memory, "w", zipfile.ZIP_DEFLATED) as z:
        for name, b in files.items():
            z.writestr(name, b)
    return memory.getvalue()


def wire():
    return {
        "evidence": "wire_only_unqualified", "svWireConsistent": True,
        "ptpPacketsVisible": True, "ptpClockSynchronized": None,
        "svPtpPhaseQualified": None, "iedAlarmFree": None,
        "svAsdus": 8000, "smpSynch0": 8000, "smpSynch1": 0,
        "smpSynch2": 0, "smpSynchUnknown": 0,
        "gaps": 0, "duplicates": 0, "outOfOrder": 0,
        "configurationChanges": 0, "counterZeroCaptureObservations": 2,
        "normalWraps": 1,
    }


class BenchEvidenceTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = Path(tmp.name) / "bench.zip"
        self.path.write_bytes(make_zip())

    def change(self, fn):
        self.path.write_bytes(make_zip(fn))

    def test_two_roles_distinct_and_verified(self):
        s = m3e.verified_bundle(self.path, "source")
        r = m3e.verified_bundle(self.path, "receiver")
        self.assertEqual(s["status"], "VERIFIED")
        self.assertEqual(s["prHeadCommit"], r["prHeadCommit"])
        self.assertNotEqual(s["appSha256"], r["appSha256"])
        self.assertEqual(r["offsets"], ["0x2000", "0x8000", "0x10000"])

    def test_modified_image_rejected(self):
        self.change(lambda f: f.__setitem__(
            "receiver/build/arstack_esp32p4_smv_injector.bin", b"corrupt"))
        with self.assertRaisesRegex(m3e.EvidenceError, "hash/length"):
            m3e.verified_bundle(self.path, "source")

    def test_config_mutation_rejected(self):
        def change(f):
            f["source/sdkconfig"] = f["source/sdkconfig"].replace(
                b"CONFIG_ETH_TRANSMIT_MUTEX=y", b"CONFIG_ETH_TRANSMIT_MUTEX=n")
        self.change(change)
        with self.assertRaisesRegex(m3e.EvidenceError, "sdkconfig"):
            m3e.verified_bundle(self.path, "receiver")

    def test_duplicate_zip_entry_rejected(self):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(self.path, "a") as z:
                z.writestr("receiver/manifest.json", b"{}")
        with self.assertRaisesRegex(m3e.EvidenceError, "duplicate ZIP"):
            m3e.verified_bundle(self.path, "receiver")

    def test_traversal_rejected(self):
        self.change(lambda f: f.__setitem__("source/../../invalid", b"x"))
        with self.assertRaisesRegex(m3e.EvidenceError, "unsafe/unexpected"):
            m3e.verified_bundle(self.path, "receiver")

    def test_role_binary_contamination_rejected(self):
        def change(f):
            app = "arstack_esp32p4_smv_injector.bin"
            payload = f[f"source/build/{app}"]
            f[f"receiver/build/{app}"] = payload
            meta = json.loads(f["receiver/manifest.json"])
            for image in meta["flashImages"]:
                if image["flashOffset"] == "0x10000":
                    image["bytes"] = len(payload)
                    image["sha256"] = m3e.sha256(payload)
            f["receiver/manifest.json"] = json.dumps(meta).encode()
        self.change(change)
        with self.assertRaisesRegex(m3e.EvidenceError, "identical SOURCE/RECEIVER"):
            m3e.verified_bundle(self.path, "source")

    def test_serial_build_id_role_and_gm_validation(self):
        log = (
            "ARSTACK identity product=SMV-INJECTOR build=" + SHA[:16] +
            " boot_id=123\nPTP2 role=RECEIVER discipline=LOCKED "
            "source=0102030405060708/2 offset=500\n").encode()
        self.assertEqual(m3e.serial_evidence(
            log, "receiver", SHA[:16])["reportedLockSamples"], 1)
        with self.assertRaisesRegex(m3e.EvidenceError, "build ID mismatch"):
            m3e.serial_evidence(log.replace(SHA[:16].encode(), b"0" * 16),
                                "receiver", SHA[:16])
        with self.assertRaisesRegex(m3e.EvidenceError, "role mismatch"):
            m3e.serial_evidence(log, "source", SHA[:16])
        with self.assertRaisesRegex(m3e.EvidenceError, "without a selected GM"):
            m3e.serial_evidence(log.replace(b"0102030405060708/2", b"NONE"),
                                "receiver", SHA[:16])

    def test_unlock_holdover_never_lock(self):
        log = ("ARSTACK identity build=" + SHA[:16] + "\n"
               "PTP2 role=RECEIVER discipline=ACQUIRING source=NONE offset=NA\n"
               "PTP2 role=RECEIVER discipline=HOLDOVER source=NONE offset=NA\n").encode()
        self.assertEqual(m3e.serial_evidence(
            log, "receiver", SHA[:16])["reportedLockSamples"], 0)

    def test_wire_fail_closed_smp_synch(self):
        a = wire()
        self.assertEqual(m3e.wire_evidence(a)["status"], "CONSISTENT")
        a["smpSynch0"] -= 1
        a["smpSynch2"] += 1
        self.assertEqual(m3e.wire_evidence(a)["status"], "REJECTED")
        a["svPtpPhaseQualified"] = True
        with self.assertRaisesRegex(m3e.EvidenceError, "unmeasured hardware"):
            m3e.wire_evidence(a)

    def test_short_capture_rejected(self):
        a = wire()
        a["counterZeroCaptureObservations"] = 1
        a["normalWraps"] = 0
        self.assertEqual(m3e.wire_evidence(a)["status"], "REJECTED")

    def test_asdu_accounting_must_match(self):
        a = wire()
        a["svAsdus"] = 20
        with self.assertRaisesRegex(m3e.EvidenceError, "ASDU sync accounting"):
            m3e.wire_evidence(a)


if __name__ == "__main__":
    unittest.main()
