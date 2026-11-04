# SV wire metadata / analyzer interoperability — 2026-11-04

## Independent wire evidence
Operator-supplied 35.03 s classic Ethernet PCAP: 140,116 SV frames, approximately 3,999.91 frame/s, no sample-counter gaps, duplicates or malformed frames in the independent report. One ASDU per frame; each seqData is 64 bytes of eight signed INT32 + UINT32 quality pairs. The current capture sends svID=ARSTACK_SV01, confRev=1, smpCnt with 4,000-count rollover, and smpSynch 0/2. It contains **no ASDU datSet [1], smpRate [6], or smpMod [8]**; it does contain optional gmIdentity [9] despite the bundled SCL setting synchSourceId=false. This capture **does not** establish global time synchronization.

The older analyzer screenshot says `Mode: Live` and `Scope view · Synthetic locked timebase`; this is **not** the current SVGen WPF raw analyzer (`RawAnalyzerDataSource`). Update the app to the exact tested PR #3 preview before interpreting the new raw waveform path.

## Standards and profile truth
IEC 61850-9-2 Ed2.1 ASDU requires svID, smpCnt (two octets), confRev (four octets), smpSynch (one octet), seqData. datSet, smpRate, smpMod and gmIdentity are OPTIONAL on the wire; their field presence must match a supported SCL SmvOpts profile and the receiving device's expected dataset.

- The **untagged ARStack lab Quick Start** now explicitly selects `SmvOpts dataSet=true sampleRate=true`; it emits configured datSet, smpRate=4000, smpMod=1 (SmpPerSec) through the existing immutable binary profile pipeline.
- The **legacy 9-2LE reference** retains its minimal optional-field policy for receivers that require the fixed legacy shape.
- Canonical BER ASDU now encodes smpCnt=2 bytes, confRev=4, smpSynch=1, smpRate=2 and smpMod=2; firmware still prebuilds packet bytes before 4 kHz TX.
- Do not advertise globally synchronized (2) from an internally generated lab grandmaster. Local measured phase/MAC evidence may support 1, otherwise advertise 0. The unsupported synchSourceId option does not authorize gmIdentity [9]. No real UTC traceability, PTP lock, synchronized MAC egress, accuracy or calibration is claimed.
- The original capture will not gain fields retroactively. To apply new SCL options, operator must **STOP SV**, load the updated lab SCL profile, inspect compiled values, **Deploy with authoritative readback**, then operator **START**. To apply codec/synchronization changes, a revision-qualified firmware update with full stopped-board proof and physical re-test is separately required; never auto-flash or replace a running board.

## Independent acceptance
Run `python tools/smv_wire_capture_audit.py <capture.pcap> --minimum-seconds 10 --require-metadata` for metadata-enabled lab profile. This requires both field presence and rate/mode consistency and fails when fields are absent; legacy captures can still pass ordinary parser-only acceptance with no `--require-metadata`. Use `--require-unsynchronized` only in a lab where no qualified source reference exists. Failures do not mean every standards-compliant legacy ASDU must contain optional fields.

For physical acceptance: corroborate exact source MAC/APPID/VLAN/VID/PCP, svID/confRev/ASDU metadata, SCL DataSet leaf sequence, 4k fps and smpCnt 3999→0, per-channel RMS/phase and quality, capture-drop counters, PTP state and independent UTC traceability. Do not convert synthetic/unit/CI evidence into an interoperability certification.
