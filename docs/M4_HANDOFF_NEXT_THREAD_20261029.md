# ARStack61850 M4 — HANDOFF OPERASIONAL UNTUK THREAD BARU
**Snapshot GitHub: 29 Oktober 2026 (WIB) | Baca ini lebih dulu, lalu verifikasi ulang GitHub sebelum menulis kode.**

> **Sumber kebenaran:** [ARD M4](ARD_M4_THREE_SV_SCL_SHARED_PTP_STUDIO.md), [AGENTS.md](../AGENTS.md), [tracking #333](https://github.com/masarray/arstack61850/issues/333), dan **HEAD `main` saat eksekusi**. Dokumen ini adalah checkpoint implementasi, bukan pengganti ARD. ARD dibuat sebelum implementasi dan judul/status historisnya bisa tertinggal; jangan mengartikan `NOT YET IMPLEMENTED` di header ARD sebagai status GitHub sekarang.

## 0. STATUS SINGKAT DAN PERINGATAN KERAS

**Repo:** `masarray/arstack61850` · branch `main`. **Main yang diverifikasi saat handoff:** `8c71efaa1ca61f474e6c647f62ed5748211ff46e` (squash merge **P0E3B2 PR #349**). **Jangan mengulang P0D2/P0D3/P0E1/P0E2A/B/C/P0E3A/B1/B2**; semuanya telah digabungkan. **M3.H.3 Official Stable** tag `processbus-sim-m3h3-20261025` beserta artifact released harus tetap immutable.

**Pekerjaan aktif thread lain per snapshot:**
- [PR #350](https://github.com/masarray/arstack61850/pull/350), `feat/m4-p0e3b3-scd-board-roster-reconcile`, **P0E3B3 exact board/SCD reconciliation: OPEN, NOT MERGED**, head saat dicek `8fc112cd644e52e8ed153d12614361190d4631c6`. Pemeriksaan Qt, firmware, Windows lifecycle, Linux package telah lulus; **Windows native preview masih in progress** pada pengecekan. Review P2 tentang invalidasi roster setelah deploy B telah ditangani/ditandai resolved; **audit ulang kode & review sebelum merge**. Thread baru **jangan membuat PR paralel dengan scope yang sama**.
- [PR #330](https://github.com/masarray/arstack61850/pull/330): LAN SV/PTP autonomy lintas USB disconnect, **OPEN, terpisah**. Jangan mengubah/merge scope milik thread itu tanpa audit dependency dan persetujuan arsitektur.
- Verifikasi ulang semua status setiap mulai kerja. Bila #350 telah merged, gunakan commit main terbaru dan **langsung naik** ke milestone sesudahnya.

**Belum selesai:** rekonsiliasi SCD/board yang sudah merged, proof 3 bank lengkap pada saat reconnect, **verified board-owned group START/STOP** dan **pengujian fisik** tiga SV 4,000 fps masing-masing + satu PTP epoch. **CI GREEN ≠ bukti real 12,000 fps di Ethernet.**

## 1. PRODUK YANG HARUS DIHASILKAN MENURUT ARD

Satu board **ESP32-P4** menghasilkan **3** IEC 61850 Sampled Values 4I+4V independen dengan **1 GUI ARStack Studio**, tiga halaman MU1/MU2/MU3 untuk Table/Waveform/Phasor, **1 otoritas PTP, 1 GPTimer ISR, 1 publisher task, 1 deterministik 250-us sample grid, 1 jalur Ethernet TX**, satu `DeviceIoWorker` pemilik serial. Semua frame pada tick yang sama mempunyai referensi sampel dan `smpCnt` sama; Ethernet tetap mengirimnya **berurutan**, sehingga egress latency antarpaket harus diukur, tidak boleh diklaim nol. Tiga bank I/U, phase/DDS, quality, CT shaping, enable/generation, identitas wire, dan stats harus independen; stop/ubah satu MU jangan mereset MU lain.

**Kontrak SCD asli di ARD (pertahankan nilai, jangan berimprovisasi):**

| MU | IED | svID | MAC multicast | APPID |
| --- | --- | --- | --- | --- |
| 1 | `MU01_4I_4V_1` | `MU01_SV1` | `01:0C:CD:04:00:01` | `0x4000` |
| 2 | `MU01_4I_4V_2` | `MU01_SV2` | `01:0C:CD:04:00:02` | `0x4000` |
| 3 | `MU01_4I_4V_3` | `MU01_SV3` | `01:0C:CD:04:00:03` | `0x4000` |

Ketiganya: VLAN **present**, VID `0`/**priority-tagged**, PCP `4`, `confRev=1`, `smpRate=4000`/`SmpPerSec`, `nofASDU=1`, counter modulo 4,000 (melalui validated 9-2LE profile choice), delapan `INT32 + Quality` pairs = payload **64 byte**. APPID sama **sengaja** sesuai SCD; jangan diganti ke 0x4001/0x4002. Tiga IED berbeda dengan `MSVCB01` sama tetap tiga stream, bukan satu stream. SCD tidak menentukan sumber PTP: jangan memalsukan PTP field dari XML. Target **3 × 4000 = 12.000 canonical SV frames/s**, plus PTP.

## 2. BASELINE YANG SUDAH MERGED — JANGAN DIULANG

| Gate | PR | Implementasi sudah di `main` |
| --- | --- | --- |
| P0D2/D3 | [#341](https://github.com/masarray/arstack61850/pull/341), [#342](https://github.com/masarray/arstack61850/pull/342) | Firmware MU1/MU2/MU3 isolated signal banks, atomic scenario transaction per slot, common publisher/PTP/GPTimer epoch dan per-stream TX gating. Belum bukti performa fisik. |
| P0E1 | [#343](https://github.com/masarray/arstack61850/pull/343) | Satu Studio dengan 3 halaman draft independen; existing Table/Waveform/Phasor reuse. |
| P0E2A | [#344](https://github.com/masarray/arstack61850/pull/344) | `SclProfileModel` mengompilasi tiga profil SCD menjadi satu bounded `ARM3 V2` envelope + hash, true MAC/svID/VLAN/APPID. |
| P0E2B | [#345](https://github.com/masarray/arstack61850/pull/345) | `M4MultiSvUploadTransaction` ACK-driven `STATUS/ABORT/BEGIN/48B CHUNK/VALIDATE/COMMIT/SHOW/SELECT` dengan CRC/hash/generation/3-wire-identity readback. |
| P0E2C1 | [#346](https://github.com/masarray/arstack61850/pull/346) | Uploader diintegrasikan ke **single** `DeviceController`/`DeviceIoWorker` dan GUI Deploy; serial TX confirmation, deadline, boot/session guards, STOP/F6. |
| P0E3A | [#347](https://github.com/masarray/arstack61850/pull/347) | `SVSTREAM 1..3 CFG` terisolasi (BEGIN/FREQ/8 SET/SHAPE/COMMIT); tiap bank butuh COMMITTED + READBACK token/hash/generation; pending CFG tail retired pada cancel. |
| P0E3B1 | [#348](https://github.com/masarray/arstack61850/pull/348) | Firmware `SVSTREAM <slot> SHOW` snapshot atomik BEGIN/8 ROW/END termasuk freq, 8 value, phase, quality, enable, CT shaping, TX enable, generation+hash. Existing Studio **Inspect board**, read-only, boleh saat board RUNNING/tanpa SCD, tidak menimpa GUI draft. |
| P0E3B2 | [#349](https://github.com/masarray/arstack61850/pull/349) | **Merged `8c71efaa1ca61f474e6c647f62ed5748211ff46e`**. Reconnect/boot mem-probe `MULTISV SHOW` dengan `PENDING/SELECTED/LEGACY/UNKNOWN`; fail closed pada timeout/malformed; mencegah V1 automatic PROFILE mengubah roster aktif; SELECTED menjalankan read-only 3-MU bank inspection. Dua S8 regression PASS. Semua 9 check ter-eksekusi SUCCESS (publish release skipped). |

**Pembedaan proof:** `M4 roster VERIFIED` hanya berarti konfigurasi profil/identitas terbaca; `3/3 bank READBACK` hanya membuktikan snapshot setpoint per slot; `SCD MATCHED` pada #350 (kalau merged) hanya membuktikan identitas roster sama. **Tak satu pun otomatis mengizinkan START, menyatakan PTP qualified, atau membuktikan traffic real.**

## 3. PETA SUMBER KODE (BACA SEBELUM MENGUBAH)

- **Kontrak arsitektur:** `docs/ARD_M4_THREE_SV_SCL_SHARED_PTP_STUDIO.md`; **aturan production coding:** `AGENTS.md`; issue [#333](https://github.com/masarray/arstack61850/issues/333).
- **SCL & canonical ARM3:** `apps/arstack_studio/src/SclProfileModel.{hpp,cpp}`, `include/ariec61850/scl/`, `include/ariec61850/sampled_values/compiled_device_profile.hpp`; cache decode/compile saat engineering source berubah, bukan pada setiap QML frame.
- **Upload/identity proof:** `apps/arstack_studio/src/M4MultiSvUploadTransaction.{hpp,cpp}`, `M4MultiSvUploadHarness.cpp` dan exact 3-stream validation (gunakan ulang parser/validator **yang sama**; jangan parser kedua).
- **Isolated bank transaction/readback:** `M4ThreeMuScenarioApply.{hpp,cpp}`, `M4ThreeMuBoardReadback.{hpp,cpp}`; firmware `embedded/esp32p4_smv_injector/main/multi_profile_control.cpp` dan stream CFG/SHOW handlers.
- **Single-writer authority + reconnect:** `DeviceController.{hpp,cpp}`, `DeviceIoWorker.{hpp,cpp}`, `SmartSessionController.{hpp,cpp}`. M4 mode `UNKNOWN/PENDING` **tidak boleh** fallback diam-diam ke V1; pending serial write perlu completion/retire ownership. STOP emergency priority tetap berlaku.
- **GUI:** `apps/arstack_studio/qml/Main.qml` (MU pagination/Deploy/Apply/Inspect); existing `SignalMatrix.qml` dan `SignalPreview.qml`; jangan buat 3 active view/model renderer per tick.
- **Regression:** `apps/arstack_studio/src/DeterministicSessionHarness.cpp`, existing `arstack_m4_upload_harness`, Linux/Windows Qt smoke, firmware ESP32-P4 & packet/clock tests. Tambah kasus pada harness existing, **jangan tambah workflow baru** tanpa justifikasi.
- **Firmware hot path:** `embedded/esp32p4_smv_injector/main/app_main.cpp` (shared timer, publisher task, PTP/EMAC). Hati-hati scope PR #330 yang sedang aktif.

## 4. URUTAN PEKERJAAN PADA THREAD BARU — MILESTONE BESAR

### A. Tutup P0E3B3 (#350), jangan duplikasi

1. **Refresh live GitHub**: `main` SHA, semua PR open, #350 head, CI exact-head, unresolved review, diff/changed files. Jangan langsung percaya snapshot ini.
2. Jika #350 masih OPEN, audit **reconnect roster invalidation ketika deploy ARM3 baru**: roster A → deploy B → SCD A harus `MISMATCH`/unverified, SCD B harus `MATCHED` hanya setelah authoritative SHOW B. Review P2 menyebut bug stale A; thread ini sudah menandai fix sebagai resolved, tetapi periksa source dan regression.
3. Validasi hash **full ARM3 binary** dan exact **MU1/2/3** `svID/MAC/APPID/VLAN present+VID+PCP/rate/wrap/noASDU/confRev`. Reject duplicate/missing slots, hash mismatch, bad CRC, boot change, stale upload readback, reordering ambigu, SCD invalid/absent. Perhatikan `NO SCD` vs `MISMATCH` vs `NOT VERIFIED`.
4. Merge hanya jika **semua executed exact HEAD CI success**, unresolved reviews nol, `main` compatible, menggunakan `expected_head_sha`. Jangan re-run job yang sudah hijau. Jika #350 sudah merged, skip seluruh fase ini.

### B. M4-P0E3C — authoritative board-owned group START/STOP (bukan START V1 terselubung)

**Invariants sebelum memberi izin group START:** verified device + current boot/session; **selected** three-stream roster, exact live SCD match (bukan GUI draft label), tiga independent bank/readback proofs yang current; no partial/retiring CFG or competing upload; physical ETH link/runtime preflight; board-owned STOPPED / PTP policy proof sesuai capability; no legacy PROFILE/START reselection, no stale generation. PTP qualified/smpSynch harus nyata dan sesuai policy, jangan menganggap PTP setting berarti lock.

Rancang command/reply state machine **sekali** untuk group operation melalui existing serial worker, dengan request-id/epoch, bounded ACK timeout, authoritative board RUNNING/STOPPED + per-slot TX-enable and generation readback. Pisahkan **accepted command** dari **observed running**, dari **wire-captured 12kfps**. On explicit STOP: priority, retire unsent actions, do not restart otomatis. On serial disconnect/GUI close: **do not issue implicit STOP**; running state belongs to firmware (per ARD, audit PR #330 semantics). On reboot/changed SCD/hash, stale ACK, TX failure, link drop/PTP epoch discontinuity: revoke proof; prevent unsafe START; no invented successful rollback if a command already reached firmware. Implement common group tick boundary so enabled MU adopts existing `smpCnt` and does not reset siblings.

**Accepted outcome P0E3C:** one intended operator group START can be accepted only after typed proofs, board reports 3/3 enabled and common publisher RUNNING; explicit group STOP readback is authoritative; shutdown/reconnect never overwrites live board. Add negative tests: absent SCD, foreign roster, partial bank, boot reset, timeout, stale ACK, STOP during START, serial disconnect, ethernet PHY down, wrong capabilities; test V1 regression and non-running board.

### C. M4-P0F — matched build dan pengujian real wire (WAJIB sebelum klaim selesai M4)

Buat matched firmware/Studio engineering release candidate; jangan ubah tag/artifact M3.H.3. Gunakan normal Ethernet switch, NIC, Wireshark/Npcap: tiga `0x88BA` MAC/svID/VLAN/APPID sesuai SCD; **4000 canonical frames/s per stream × 3**, selama interval representatif dan tahan lama; tiap tick sama `smpCnt` modulo 4000 dari **1 common PTP epoch**; 8 value + quality per MU berbeda; `0x88F7` PTP ditangkap bila berjalan. Ukur batch/EMAC TX duration worst-case, egress separation, jitter, missed, overrun, link-down, loss/reorder/duplicates, RAM/CPU/stack headroom, 100-Mbit traffic budget. **Simpan PCAP + script/ringkasan per stream + board diagnostics + exact build/boot/hash**.

Budget ARD `<150us` per 250us tick adalah **target**, bukan hasil pengukuran. 3 packet TX pada satu MAC berurutan, jangan klaim simultan di kawat. Capture lama pada #330 yang menunjukkan **satu** SV stream/berhenti prematur **bukan** bukti M4 tiga stream.

## 5. ATURAN CODING: PRODUCTION, OPTIMIZED, MAINTAINABLE

- Ikuti `AGENTS.md` urut: **reconnaissance → baseline/reproduce → root cause → invariant → architecture impact → implement → deterministic/negative regression → timing/performance check → CI → interoperability**. Jangan tambal gejala dengan sleep/retry acak.
- **Satu SSOT** per fakta: canonical SCD-derived ARM3; firmware authoritative roster/SHOW; firmware atomic bank + hash/generation; satu session owner. Board readback **tidak ditulis otomatis ke draft**; draft UI tidak pernah diklaim runtime truth.
- Fixed capacity **3** runtime slots M4.0, no unbounded `vector`/queues, no copy/paste 3 parser/worker/timer/publisher; satu GPTimer, satu PTP authority, satu publisher, satu serial worker. No heap/new, log, locks, XML, trigonometri mahal atau QML di steady-state 250us hot path. Reuse compiled packet template, immutable layouts, LUT dan existing signal bank/DDS.
- Setiap state change dibatasi boot ID, generation, transaction token, clock/deadline. Cancellation harus menghapus queue commands yang **belum terkirim** dan menahan ownership sampai retired; command sudah keluar UART ⇒ board state **uncertain until new SHOW**. STOP emergency tidak menunggu queue biasa.
- Preserve **V1/M3.H.3 compatibility**, runtime LAN autonomy, separate PTP STOP and SV STOP, Qt/Windows/Linux lifecycle. Don't enable/group START from a status badge or a mocked ACK.
- Uji dalam **satu meaningful PR** berisi full feature + negative regression + docs + screenshots bila GUI; bukan deretan micro-commits/PR kosmetik. Hindari menambah workflow/runner/matrix, gunakan existing Qt/firmware/Windows/native preview pipelines. **CI economy:** offline design dahulu, sekali head CI untuk batched change; jika memang bug, perbaiki terlokalisasi; retry **failed** job saja bila transient tanpa perubahan kode; audit exact-head CI dan review sebelum squash-merge. Jangan merun ulang semua checks karena impatience.
- Tidak ada klaim “hardware proven” tanpa PCAP/NIC dan board log. Bila tidak tersedia perangkat, hasilnya hanya `CI VERIFIED / HARDWARE PENDING`.

## 6. FIRST ACTION UNTUK THREAD BARU (DO, NOT RESTART)

1. Baca `AGENTS.md`, ARD ini, handoff ini; baca **current issue #333** dan dokumentasi implementasi P0D3/P0E2B/P0E3A/B1/B2.
2. Query **live** `main`, open PR, last reviews/CI; **jangan pakai SHA snapshot sebagai HEAD permanen**.
3. Periksa #350, resolve only evidence-backed problems, merge only if exact-head gates pass. Jangan buat branch baru P0E3B3 karena orang lain sudah mengerjakannya.
4. Setelah main stabil, prioritaskan **M4-P0E3C complete controlled group START/STOP + board readback** sebagai milestone fungsional besar. Re-audit firmware existing START dispatch dan PR #330 sebelum mendesain; pertahankan single-owner dan shared epoch.
5. Turunkan acceptance tests dari ARD; jalankan existing harnesses dan hardware/packet tests sesuai pekerjaan; jangan mengklaim real throughput dari CI.
6. Laporkan hasil berupa **PR/commit/CI exact head**, fitur nyata, negative cases, keterbatasan hardware, dan next gate. Update handoff ini dan #333 dengan status terbaru saat merge.

## 7. REFERENCE

- **Engineering ARD:** [ARD M4 Three SV / Shared PTP / Studio](ARD_M4_THREE_SV_SCL_SHARED_PTP_STUDIO.md)
- **Global coding:** [AGENTS.md](../AGENTS.md)
- **Tracking:** [Issue #333](https://github.com/masarray/arstack61850/issues/333)
- **Current P0E3B3 under PR:** [#350](https://github.com/masarray/arstack61850/pull/350), [its guide (branch)](https://github.com/masarray/arstack61850/blob/feat/m4-p0e3b3-scd-board-roster-reconcile/docs/M4_P0E3B3_SCD_ROSTER_RECONCILIATION.md)
- **Autonomy in parallel:** [#330](https://github.com/masarray/arstack61850/pull/330)
- **Recent closed gates:** [#348](https://github.com/masarray/arstack61850/pull/348), [#349](https://github.com/masarray/arstack61850/pull/349)
- **Historical handoff (older status, not current):** [2026-10-26 checkpoint](M4_HANDOFF_TO_NEXT_THREAD_20261026.md)

**Satu kalimat arah:** jangan mulai ulang M4; selesaikan verifikasi SCD/firmware roster dengan CI sah, lalu wujudkan operator-controlled **3-MU GROUP START/STOP** yang mendapat authoritative board readback, selanjutnya buktikan **3×4000fps/1 PTP sample epoch** memakai **real wire evidence**.
