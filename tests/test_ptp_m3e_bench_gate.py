#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic positive and adversarial M3E host evidence regression."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import sys
import unittest
import zipfile

S = importlib.util.spec_from_file_location(
    "ptp_m3e_bench_gate", Path(__file__).resolve().parents[1] / "scripts" / "ptp_m3e_bench_gate.py"
)
module = importlib.util.module_from_spec(S)
S.loader.exec_module(module)
HEAD = "7d2d5146b1892124c1744ad26ab31b3489e8387e"


def archive(*, change=None):
    entries = {}
    for role, value in (("source", 0), ("receiver", 1)):
        config = ('CONFIG_IDF_TARGET="esp32p4"\n'
                  f'CONFIG_AR_PTP_OPERATING_ROLE={value}\n'
                  'CONFIG_AR_PTP_LAB_TX=y\nCONFIG_AR_SMV_LIVE_TX=y\n'
                  'CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL=y\n'
                  'CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL=y\n'
                  'CONFIG_ETH_TRANSMIT_MUTEX=y\n'
                  '# CONFIG_AR_SMV_BROADCAST_MIRROR is not set\n'
                  '# CONFIG_AR_SMV_DIAGNOSTIC_PROBES is not set\n').encode()
        flash_files = {
            "0x2000": "bootloader/bootloader.bin",
            "0x8000": "partition_table/partition-table.bin",
            "0x10000": "arstack_esp32p4_smv_injector.bin",
        }
        images = []
        for offset, name in flash_files.items():
            content = (HEAD[:16].encode() + role.encode() + b" APP TEST") if "/" not in name else name.encode()
            entries[f"{role}/build/{name}"] = content
            images.append({"flashOffset": offset, "relativeFile": "build/" + name,
                           "bytes": len(content), "sha256": module.sha256(content)})
        manifest = {
            "kind": "ARStack61850 experimental PTP bench-only; NOT a stable release",
            "boardTarget": "esp32p4", "role": role, "prHeadCommit": HEAD,
            "checkoutCommit": "c" * 40, "semanticBuildId": HEAD[:16],
            "hardwareValidated": False, "smpSynchAutoQualification": False,
            "flashImages": images, "sdkconfigSha256": module.sha256(config),
        }
        entries[f"{role}/sdkconfig"] = config
        entries[f"{role}/flasher_args.json"] = json.dumps({"flash_files": flash_files}).encode()
        entries[f"{role}/manifest.json"] = json.dumps(manifest).encode()
    if change:
        change(entries)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as out:
        for path, value in entries.items():
            out.writestr(path, value)
    return buf.getvalue()


def wire_json():
    return {"evidence": "wire_only_unqualified", "svWireConsistent": True,
            "ptpPacketsVisible": True, "ptpClockSynchronized": None,
            "svPtpPhaseQualified": None, "iedAlarmFree": None,
            "svAsdus": 8001, "smpSynch0": 8001, "smpSynch1": 0,
            "smpSynch2": 0, "smpSynchUnknown": 0,
            "gaps": 0, "duplicates": 0, "outOfOrder": 0,
            "configurationChanges": 0, "counterZeroCaptureObservations": 2,
            "normalWraps": 1}


class BenchGateTests(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.addCleanup(self.t.cleanup)
        self.path = Path(self.t.name) / "firmware.zip"
        self.path.write_bytes(archive())

    def rewrite(self, change):
        self.path.write_bytes(archive(change=change))

    def test_two_role_artifact_positive(self):
        src = module.verified_bundle(self.path, "source")
        rx = module.verified_bundle(self.path, "receiver")
        self.assertEqual(src["status"], "VERIFIED")
        self.assertEqual(src["prHeadCommit"], rx["prHeadCommit"])
        self.assertNotEqual(src["appSha256"], rx["appSha256"])
        self.assertEqual(rx["offsets"], ["0x2000", "0x8000", "0x10000"])

    def test_tampered_receiver_bin_fails_closed(self):
        self.rewrite(lambda e: e.__setitem__("receiver/build/arstack_esp32p4_smv_injector.bin", b"tampered"))
        with self.assertRaisesRegex(module.EvidenceError, "hash/length"):
            module.verified_bundle(self.path, "source")

    def test_tampered_config_fails_closed(self):
        def mutate(e):
            e["source/sdkconfig"] = e["source/sdkconfig"].replace(
                b"CONFIG_ETH_TRANSMIT_MUTEX=y", b"CONFIG_ETH_TRANSMIT_MUTEX=n")
        self.rewrite(mutate)
        with self.assertRaisesRegex(module.EvidenceError, "sdkconfig"):
            module.verified_bundle(self.path, "receiver")

    def test_duplicate_zip_entries_fail(self):
        with zipfile.ZipFile(self.path, "a") as z:
            z.writestr("receiver/manifest.json", b"{}")
        with self.assertRaisesRegex(module.EvidenceError, "duplicate ZIP"):
            module.verified_bundle(self.path, "receiver")

    def test_path_traversal_fails(self):
        self.rewrite(lambda e: e.__setitem__("source/../../malicious", b"x"))
        with self.assertRaisesRegex(module.EvidenceError, "unsafe/unexpected"):
            module.verified_bundle(self.path, "receiver")

    def test_cross_role_app_contamination_fails(self):
        def mutate(e):
            role = "receiver"
            e[f"{role}/build/arstack_esp32p4_smv_injector.bin"] = e["source/build/arstack_esp32p4_smv_injector.bin"]
            m = json.loads(e[f"{role}/manifest.json"])
            for f in m["flashImages"]:
                if f["flashOffset"] == "0x10000":
                    f["sha256"] = module.sha256(e["source/build/arstack_esp32p4_smv_injector.bin"])
                    f["bytes"] = len(e["source/build/arstack_esp32p4_smv_injector.bin"])
            e[f"{role}/manifest.json"] = json.dumps(m).encode()
        self.rewrite(mutate)
        with self.assertRaisesRegex(module.EvidenceError, "identical SOURCE/RECEIVER"):
            module.verified_bundle(self.path, "source")

    def test_wrong_serial_build_or_role_rejected(self):
        good = b'I (0) ar: ARSTACK identity product=SMV-INJECTOR build=7d2d5146b1892124 boot_id=123\nI (0): PTP2 role=RECEIVER discipline=LOCKED source=0102030405060708/2 offset=500\n'
        result = module.serial_evidence(good, "receiver", HEAD[:16])
        self.assertEqual(result["reportedLockSamples"], 1)
        with self.assertRaisesRegex(module.EvidenceError, "build ID mismatch"):
            module.serial_evidence(good.replace(b"7d2d5146b1892124", b"0000000000000000"), "receiver", HEAD[:16])
        with self.assertRaisesRegex(module.EvidenceError, "role mismatch"):
            module.serial_evidence(good, "source", HEAD[:16])
        with self.assertRaisesRegex(module.EvidenceError, "without a selected GM"):
            module.serial_evidence(good.replace(b"0102030405060708/2", b"NONE"), "receiver", HEAD[:16])

    def test_no_lock_claim_when_acquiring_or_holdover(self):
        text = ("ARSTACK identity build=" + HEAD[:16] + " boot_id=abcd\n" +
                "PTP2 role=RECEIVER discipline=ACQUIRING source=0102030405060708/2 offset=NA\n" +
                "PTP2 role=RECEIVER discipline=HOLDOVER source=NONE offset=NA\n").encode()
        result = module.serial_evidence(text, "receiver", HEAD[:16])
        self.assertEqual(result["reportedLockSamples"], 0)

    def test_wire_positive_and_false_smp_synch_rejection(self):
        evidence = wire_json()
        self.assertEqual(module.wire_evidence(evidence)["status"], "CONSISTENT")
        evidence["smpSynch0"] -= 1
        evidence["smpSynch2"] += 1
        self.assertEqual(module.wire_evidence(evidence)["status"], "REJECTED")
        evidence["svPtpPhaseQualified"] = True
        with self.assertRaisesRegex(module.EvidenceError, "unmeasured hardware"):
            module.wire_evidence(evidence)

    def test_short_capture_not_consistent(self):
        evidence = wire_json()
        evidence["counterZeroCaptureObservations"] = 1
        evidence["normalWraps"] = 0
        self.assertEqual(module.wire_evidence(evidence)["status"], "REJECTED")

    def test_reject_bad_pcap_audit_accounting(self):
        evidence = wire_json()
        evidence["svAsdus"] = 22
        with self.assertRaisesRegex(module.EvidenceError, "ASDU sync accounting"):
            module.wire_evidence(evidence)




BOOT = "ABCDEF0123456789"
GM_A = "0102030405060708/1"
GM_B = "1122334455667788/2"


def ptp_line(state, gm=GM_A, *, complete=True, accepted=8):
    base = f"PTP2 role=RECEIVER discipline={state} source={gm}"
    if not complete:
        return base + " offset=NA"
    return (base + " offset=200 path=100 jitter=5 freq=0 global=0 measured=NA "
            "rxAnnounce=8 rxSync=8 rxFollowUp=8 rxPdelay=8 pdelayReq=8 "
            f"accepted={accepted} rejected=0")


def bench_log(*states, gm_seq=None, boot=BOOT):
    messages = [f"ARSTACK identity build={HEAD[:16]} boot_id={boot}"]
    for i, state in enumerate(states):
        gm = gm_seq[i] if gm_seq else GM_A
        messages.append(ptp_line(state, gm))
    return ("\n".join(messages) + "\n").encode()


class GateReportIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.path = Path(self.dir.name)
        self.bundle = self.path / "bundle.zip"
        self.bundle.write_bytes(archive())
        self.log = self.path / "receiver.log"
        self.pcap = self.path / "input.pcap"
        self.pcap.write_bytes(b"a test PCAP path; mock host audit only")
        self.audit = self.path / "ariec61850_ptp_sv_wire_audit"
        self.audit.write_bytes(b"mock host C++ binary")

    def invoke(self, log, scenario="auto"):
        import contextlib
        import unittest.mock
        self.log.write_bytes(log)
        args = ["m3e", "--bundle", str(self.bundle), "--role", "receiver",
                "--serial-log", str(self.log), "--pcap", str(self.pcap),
                "--wire-audit", str(self.audit), "--sv-id", "TEST.SV",
                "--app-id", "0x4000", "--counter-modulus", "4000",
                "--ptp-domain", "0", "--scenario", scenario]
        buffer = io.StringIO()
        with unittest.mock.patch.object(sys, "argv", args), \
             unittest.mock.patch.object(module, "run_wire_audit", return_value={"status": "CONSISTENT"}), \
             contextlib.redirect_stdout(buffer):
            rc = module.main()
        return rc, json.loads(buffer.getvalue())

    def test_wire_consistency_never_covers_unlocked_receiver(self):
        code, report = self.invoke(bench_log("UNLOCKED", "ACQUIRING"))
        self.assertEqual(code, 2)
        self.assertFalse(report["softwareEvidenceConsistent"])
        self.assertEqual(report["operationalLifecycle"]["status"], "INCOMPLETE")
        self.assertFalse(report["autoSmpSynchPromotionPermitted"])

    def test_report_is_observed_not_hardware_qualified(self):
        code, report = self.invoke(bench_log("UNLOCKED", "ACQUIRING", "LOCKED"))
        self.assertEqual(code, 0)
        self.assertTrue(report["softwareEvidenceConsistent"])
        self.assertEqual(report["operationalLifecycle"]["status"], "OBSERVED")
        self.assertIsNone(report["independentHardwarePhaseQualified"])
        self.assertIsNone(report["independentPTPGrandmasterAccuracyQualified"])
        self.assertIsNone(report["relayAcceptanceQualified"])
        self.assertFalse(report["publicReleasePermitted"])

    def test_recovery_requires_second_lock_in_cli(self):
        code, report = self.invoke(
            bench_log("UNLOCKED", "ACQUIRING", "LOCKED"),
            scenario="receiver-recovery")
        self.assertEqual(code, 2)
        self.assertFalse(report["softwareEvidenceConsistent"])
        self.assertEqual(report["operationalLifecycle"]["status"], "INCOMPLETE")


class LifecycleScenarioTests(unittest.TestCase):
    def run_gate(self, data, scenario="receiver-acquisition", role="receiver"):
        return module.operational_evidence(data, role, scenario)

    def test_unlocked_only_never_qualifies_receiver(self):
        evidence = self.run_gate(bench_log("UNLOCKED", "ACQUIRING"))
        self.assertEqual(evidence["status"], "INCOMPLETE")
        self.assertEqual(evidence["reportedLockSamples"], 0)

    def test_truncated_or_unmeasured_locked_report_not_accepted(self):
        data = bench_log("UNLOCKED") + (ptp_line("LOCKED", complete=False) + "\n").encode()
        self.assertEqual(self.run_gate(data)["status"], "INCOMPLETE")
        data = bench_log("UNLOCKED") + (ptp_line("LOCKED", accepted=0) + "\n").encode()
        self.assertEqual(self.run_gate(data)["status"], "INCOMPLETE")

    def test_cold_acquisition_observed_from_ordered_evidence(self):
        actual = self.run_gate(bench_log("UNLOCKED", "ACQUIRING", "LOCKED"))
        self.assertEqual(actual["status"], "OBSERVED")
        self.assertEqual(actual["reportedLockSamples"], 1)
        self.assertFalse(actual["reportedRecovery"])

    def test_lock_without_prior_acquisition_not_an_acquisition(self):
        self.assertEqual(self.run_gate(bench_log("LOCKED"))["status"], "INCOMPLETE")

    def test_recovery_requires_demotion_and_a_second_valid_lock(self):
        self.assertEqual(self.run_gate(
            bench_log("LOCKED", "LOCKED"), "receiver-recovery")["status"], "INCOMPLETE")
        actual = self.run_gate(
            bench_log("LOCKED", "HOLDOVER", "UNLOCKED", "ACQUIRING", "LOCKED"),
            "receiver-recovery")
        self.assertEqual(actual["status"], "OBSERVED")
        self.assertTrue(actual["reportedRecovery"])

    def test_gm_switch_requires_an_intervening_demotion(self):
        actual = self.run_gate(bench_log("LOCKED", "UNLOCKED", "LOCKED",
                                        gm_seq=[GM_A, GM_A, GM_B]),
                               "receiver-gm-handoff")
        self.assertEqual(actual["status"], "OBSERVED")
        self.assertTrue(actual["reportedGmHandoff"])
        directly = self.run_gate(bench_log("LOCKED", "LOCKED",
                                           gm_seq=[GM_A, GM_B]),
                                 "receiver-gm-handoff")
        self.assertEqual(directly["status"], "INCOMPLETE")
        self.assertTrue(directly["directGmHandoffWithoutDemotion"])
        same_gm = self.run_gate(bench_log("LOCKED", "HOLDOVER", "LOCKED"),
                                "receiver-gm-handoff")
        self.assertEqual(same_gm["status"], "INCOMPLETE")

    def test_any_unfenced_direct_gm_swap_vetoes_later_good_transition(self):
        data = bench_log("LOCKED", "LOCKED", "UNLOCKED", "LOCKED",
                         gm_seq=[GM_A, GM_B, GM_B, GM_A])
        result = self.run_gate(data, "receiver-gm-handoff")
        self.assertTrue(result["reportedGmHandoff"])
        self.assertEqual(result["status"], "INCOMPLETE")

    def test_multiple_boot_sessions_cannot_be_combined(self):
        data = bench_log("UNLOCKED", "ACQUIRING") + bench_log("LOCKED", boot="1234567890ABCDEF")
        self.assertEqual(self.run_gate(data)["status"], "INCOMPLETE")

    def test_unknown_boot_id_does_not_certify_a_scenario(self):
        data = bench_log("ACQUIRING", "LOCKED", boot="NA")
        self.assertEqual(self.run_gate(data)["status"], "INCOMPLETE")

    def test_source_requires_actual_sent_ptp_triplet(self):
        base = (f"ARSTACK identity build={HEAD[:16]} boot_id={BOOT}\n"
                "PTP2 role=SOURCE discipline=UNLOCKED source=NONE offset=NA\n")
        incomplete = self.run_gate(base.encode(), "source-ready", "source")
        self.assertEqual(incomplete["status"], "INCOMPLETE")
        complete = self.run_gate((base +
            "PTP status=RUNNING Announce=4 Sync=4 FollowUp=4 PdelayFrames=0 TXfail=0\n").encode(),
            "source-ready", "source")
        self.assertEqual(complete["status"], "OBSERVED")

    def test_source_readiness_is_latest_not_any_historical_running(self):
        prefix = (f"ARSTACK identity build={HEAD[:16]} boot_id={BOOT}\n"
                  "PTP2 role=SOURCE discipline=UNLOCKED source=NONE offset=NA\n")
        running = "PTP status=RUNNING Announce=4 Sync=4 FollowUp=4 PdelayFrames=0 TXfail=0\n"
        for suffix in ("PTP stop requested\n",
                       "PTP status=STOPPED Announce=4 Sync=4 FollowUp=4 PdelayFrames=0 TXfail=0\n",
                       "PTP status unavailable\n",
                       "PTP start rejected: link down\n",
                       "PTP status=RUNNING Announce=4 Sync=4 FollowUp=4 PdelayFrames=0\n"):
            with self.subTest(suffix=suffix):
                actual = self.run_gate((prefix + running + suffix).encode(),
                                       "source-ready", "source")
                self.assertEqual(actual["status"], "INCOMPLETE")
        # A later clean RUNNING status is evidence of a newly reported state.
        clean_restart = prefix + running + "PTP stop requested\n" + running
        self.assertEqual(self.run_gate(clean_restart.encode(), "source-ready",
                                       "source")["status"], "OBSERVED")

    def test_source_any_reported_tx_failure_vetoes_same_boot(self):
        prefix = (f"ARSTACK identity build={HEAD[:16]} boot_id={BOOT}\n"
                  "PTP2 role=SOURCE discipline=UNLOCKED source=NONE offset=NA\n")
        bad = "PTP status=RUNNING Announce=4 Sync=4 FollowUp=4 PdelayFrames=0 TXfail=1\n"
        good = "PTP status=RUNNING Announce=8 Sync=8 FollowUp=8 PdelayFrames=0 TXfail=0\n"
        actual = self.run_gate((prefix + bad + good).encode(), "source-ready", "source")
        self.assertEqual(actual["status"], "INCOMPLETE")
        self.assertTrue(actual["sourceTxFaultObserved"])
        self.assertEqual(self.run_gate((prefix + bad).encode(), "source-ready",
                                       "source")["status"], "INCOMPLETE")

    def test_missing_boot_id_on_later_identity_fails_closed(self):
        data = (f"ARSTACK identity build={HEAD[:16]} boot_id={BOOT}\n"
                "PTP2 role=SOURCE discipline=UNLOCKED source=NONE offset=NA\n"
                f"ARSTACK identity build={HEAD[:16]}\n"
                "PTP status=RUNNING Announce=4 Sync=4 FollowUp=4 PdelayFrames=0 TXfail=0\n")
        self.assertEqual(self.run_gate(data.encode(), "source-ready",
                                       "source")["status"], "INCOMPLETE")

    def test_incompatible_role_scenario_is_rejected(self):
        with self.assertRaisesRegex(module.EvidenceError, "incompatible"):
            self.run_gate(bench_log("LOCKED"), "source-ready", "receiver")

    def test_positive_wire_is_not_sufficient_for_unlocked_receiver(self):
        full = module.wire_evidence(wire_json())
        self.assertEqual(full["status"], "CONSISTENT")
        lifecycle = self.run_gate(bench_log("UNLOCKED", "ACQUIRING"))
        self.assertNotEqual(lifecycle["status"], "OBSERVED")

if __name__ == "__main__":
    unittest.main()