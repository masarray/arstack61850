# HANDOFF — ARStack61850 M3.H → PUBLIC STABLE + WEBSITE/README SYNC
**Tanggal:** 24 Oktober 2026 (Asia/Jakarta)  
**Status handoff:** **BELUM 100% / ACTION REQUIRED**  

> **FINAL SOURCE-IN-MAIN UPDATE (24 Oct 2026):** [PR #299](https://github.com/masarray/arstack61850/pull/299) **MERGED** to main commit `ad2ebd0e7696dcbc389f1241fedbec5ecf504881`, after exact candidate `c90a9798508fe5146d53cf3173b2980ded557198` passed **14/14 GitHub Actions workflows**. Source final `a922e9bdba4e57a4e88406eb83344455dd8ff142` and official release tag commit `a4b6e53d0dbbcababc55ba8a4ff97ade6f2a6200` are both Git ancestors of main; compare against main reports 0 behind. All current main post-release documentation preserved, implementation exactly identical to tested final source, original Studio/firmware release assets unchanged. Ancestor PRs #290–#293 and #295–#298 CLOSED as integrated through #299 after ancestry verification. **Older P2 text and PR-open snapshots below are superseded**; see [updated audit](../integration/M3H_SOURCE_STACK_AUDIT_2026-10-24.md). Divergent #289/#294/#284 remain intentionally separate. Portfolio frontend external deployment still not independently verified.

> **Update pelaksanaan 24 Oct 2026:** Existing release **ID 422544367** sudah dipromosikan ke **latest Public Stable** (`prerelease=false`) tanpa perubahan kelima aset; [bukti promotion](https://github.com/masarray/arstack61850/actions/runs/40071700074). Product URL `https://masarray.github.io/arstack61850/` dan portal sama-sama **HTTP 200**; lima download real, checksum, Build ID, dan firmware di Portable ZIP lulus [HTTP smoke](https://github.com/masarray/masarray.github.io/actions/runs/40072546814). Bug 404 produk diselesaikan dengan [GitHub Pages project deployment](https://github.com/masarray/arstack61850/actions/runs/40072480932). [Browser mobile/desktop acceptance](https://github.com/masarray/arstack61850/actions/runs/40072896355) PASS di 1440/768/390/360 px. README/dokumentasi/product/portal/portfolio source telah sinkron; [audit source PR #295–#298](../integration/M3H_SOURCE_STACK_AUDIT_2026-10-24.md) terdokumentasi tanpa merge. **Masih belum memenuhi 100% ALL-SURFACES** karena URL deployment publik dari repo `masarray/portfolio` tidak tersedia untuk verifikasi (source dan build CI PASS, deployment eksternal tidak diketahui). Seluruh checklist P0/P1 berikut adalah **snapshot saat handoff dibuat**, baca [acceptance evidence terkini](../releases/M3H_PUBLIC_STABLE_ACCEPTANCE_2026-10-24.md) untuk status aktual.

**Repository inti:** https://github.com/masarray/arstack61850  
**Prioritas:** Finalisasi produk *engineering process-bus simulator* sebagai **PUBLIC STABLE**, bukan sertifikasi GPS/relay; sinkronisasi dan pembuktian deployment README/landing pages.  
**Jangan lakukan:** memperbarui firmware PTP/SV, mengganti BIN yang berhasil diuji, mengklaim IED/relay sudah menerima trafik, atau me-merge 600+ commit secara membabi buta.

## 1. Yang sudah benar-benar selesai / diverifikasi

1. **Rilis GitHub publik ada dan mempunyai 5 asset nyata**, https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h-20261024 . **Namun GitHub menandai `prerelease=true`, bukan Stable**, sehingga permintaan user PUBLIC STABLE **BELUM SELESAI**. Rilis juga tidak menjadi latest stable; v0.1.1 tetap stable historis.
2. Source/binary yang lulus: `a922e9bdba4e57a4e88406eb83344455dd8ff142`; Build ID ESP32-P4 `a922e9bdba4e57a4`; 10/10 CI SUCCESS pada source ini. GitHub Release tag `processbus-sim-m3h-20261024` menunjuk commit dokumentasi `a4b6e53d0dbbcababc55ba8a4ff97ade6f2a6200`. Perbedaan tag-vs-binary wajar: tag hanya menambahkan docs/workflow; workflow immutable-check menolak perubahan implementasi setelah source uji. Run packaging/publish: https://github.com/masarray/arstack61850/actions/runs/40067685350 (**SUCCESS**).
3. Binary yang dipublikasikan: 
   - [Windows Setup](https://github.com/masarray/arstack61850/releases/download/processbus-sim-m3h-20261024/ARStack-ProcessBus-M3H-win-x64-setup.exe), SHA-256 `4902e8bf44e78fc55a139ce84a1273f0768ef6fe10000f5d0f85d75d2f6c736e`.
   - [Portable ZIP](https://github.com/masarray/arstack61850/releases/download/processbus-sim-m3h-20261024/ARStack-ProcessBus-M3H-win-x64-portable.zip), SHA-256 `ca01424e4102aa65e353737024ebf3258b4d2f614fa60d932c95c353ee228d8f`.
   - [ESP32-P4 Firmware](https://github.com/masarray/arstack61850/releases/download/processbus-sim-m3h-20261024/ARStack-ESP32P4-ProcessBus-M3H-firmware.bin), SHA-256 `61c508e9bf1357eb964d2b817b260b6a0e985446ddd68de1d45d9467e25c4607`.
   - `firmware-manifest.json` dan `SHA256SUMS.txt` tersedia pada halaman rilis.
4. **Bukti capture engineer M3.H**: 163.048 frame SV dalam 40,762934 detik, laju rata-rata ~3.999,884 fps, **100% smpSynch=2**, nol missing/duplicate/reorder/malformed dalam segmen aktif, Quality normal; PTP 44 Announce, 43 Sync dan 43 Follow_Up berpasangan; sequence kontinu. Capture SV STOP sebelum target 120 detik (Studio menandai permintaan 120s `INVALID_CAPTURE`), *bukan* error frame di segmen valid. Relai asli dan Pdelay receiver belum diuji. **Jangan mewajibkan GPS, instrumen penguji komersial, relai atau sertifikasi untuk public simulator release.** Catat batas bukti saja.
5. **README di repository `main` SUDAH tersinkron** ke M3.H: `README.md`, `apps/arstack_studio/README.md`, dan `PROJECT_HANDOFF.md` menyebut release/tautan terbaru. GitHub `main` saat diperiksa: `c9cb9cac1247d06f7f19a7fffe0c2026506cc8cd` (cek ulang sebelum menulis).
6. **Landing pages source SUDAH tersinkron** (jangan salah menyebut belum dikerjakan):
   - Product landing source: https://github.com/masarray/masarray.github.io/blob/main/arstack61850/index.html, target public https://masarray.github.io/arstack61850/; sudah memuat M3.H, tombol Setup/Portable/Firmware, statistik capture, PTP simulator, First Steps.
   - MasArray portal: https://github.com/masarray/masarray.github.io/blob/main/index.html, live https://masarray.github.io/ sudah menampilkan teaser M3.H beserta CTA (web retrieval terkonfirmasi).
   - Portfolio data: https://github.com/masarray/portfolio/blob/main/src/data/portfolio.ts memuat `Public Lab Edition · M3.H`, release URL, website URL. Deployment frontend portfolio belum diverifikasi.
   - Legacy browser preview https://github.com/masarray/arstack61850/blob/main/apps/smv_injector_gui/index.html kini menautkan native M3.H.
7. **Hanya rilis dan link publik, BUKAN integrasi source**: PR #295, #296, #297, #298 masih **Draft/Open** dengan branch bertingkat; `main` tidak memuat integrasi 600+ commit M3.H sekaligus. PR penting: https://github.com/masarray/arstack61850/pull/298 . Pin firmware sumber M3.H dan jangan me-merge stack tersebut tanpa rekonsiliasi/CI.
8. Freeze release docs/source: https://github.com/masarray/arstack61850/tree/release/processbus-simulator-m3h-20261024 dan https://github.com/masarray/arstack61850/blob/main/docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md .

## 2. Pekerjaan PENDING agar benar-benar 100% PUBLIC STABLE

### P0 — Status rilis GitHub **belum Stable**
- [ ] **Ubah release yang SUDAH ADA** dengan tag `processbus-sim-m3h-20261024`: dari `prerelease=true` menjadi **`prerelease=false`**, `draft=false`; set **latest stable** bila memang ini menjadi rilis default proyek. **Jangan buat release kedua atau unggah binary lain**. Pastikan 5 asset dan SHA unchanged.
- [ ] Judul/narasi rilis: *ARStack Process Bus Simulator M3.H — Stable Lab Simulator Edition*. "Stable" bermakna aplikasi/firmware emulator terdistribusi dan sesuai acceptance lokal, **bukan** GPS certified atau real-relay interoperability. Jangan hilangkan keterangan `simulated Global GM`.
- [ ] Pastikan langkah ini berhasil di API GitHub Release (`prerelease=false`, tanggal, assets) dan halaman GitHub tidak lagi bertanda **Pre-release**. Keterbatasan alat saat thread sebelumnya: tool GitHub tersedia untuk baca/artifact/kode, tetapi tidak mengekspos endpoint tulis `PATCH /releases/{id}`; jalan resmi: GitHub UI, atau workflow Actions dengan `gh release edit TAG --prerelease=false --latest` menggunakan `contents:write` dan izin yang sesuai.

### P1 — Tuntaskan konsistensi README, pages, portfolio setelah Stable
- [ ] Semua README, Product Landing, portal MasArray, portfolio harus memakai **status yang sama**: PUBLIC STABLE **Lab Simulator M3.H**, bukan "latest experimental prerelease" yang kedaluwarsa. Selalu pertahankan fakta PTP Global GM = simulator lab, bukan klaim GPS physical.
- [ ] Verifikasi **deployment LIVE** https://masarray.github.io/arstack61850/ (fetch browser sebelumnya tidak dapat mengakses langsung sehingga hanya source GitHub terbukti), respons HTTP, metadata OpenGraph/title, gambar/fit mobile, CTA dan 5 tautan release. Verifikasi halaman portal LIVE dan portfolio yang benar-benar dideploy; perbaiki jika mismatch/cache.
- [ ] Jadikan halaman rilis sebagai single source of truth untuk checksum/tag; jangan hardcode firmware lain di landing.
- [ ] Tinjau `PROJECT_HANDOFF.md` lama: masih menyimpan catatan v0.1.0/P0 `smpSynch=0` yang sifatnya historis. Jelaskan tegas historis agar pembaca tidak salah mengira itu kondisi M3.H kini.

### P2 — Source integration dan health gates (independen dari publish)
- [ ] Audit dan rencanakan konsolidasi PR bertingkat #295→#298 terhadap `main` tanpa force/squash sembarangan. `main` telah menyerap docs/landing pointer, **tetapi belum tentu code PTP/SV M3.H**. Telusuri merge-base, compare commits, reviewers/CI secara eksplisit.
- [ ] Stable release **tidak perlu menunggu alat relay, GPS atau pengujian sertifikasi**. Verifikasi realistis tersedia: paired build ID+checksum, SV/PTP PCAP 40,76s, hardware timestamp code paths, bootstrap restart, offline installer smoke, link health.
- [ ] Jika ada perubahan implementasi setelah `a922e9bd...`, jangan menyebut build lama mengandung perubahan itu; rilis M3.H tetap immutable. Perbaikan baru wajib milestone berikutnya.

## 3. Acceptance final / Definition of Done

1. `GET /repos/masarray/arstack61850/releases/tags/processbus-sim-m3h-20261024` → `draft:false, prerelease:false` + 5 assets SHA preserved; GitHub "latest" menunjuk rilis stable target.
2. https://masarray.github.io/arstack61850/ **LIVE** dan tombol Setup, Portable, Firmware bekerja, tidak sekadar ada di source.
3. https://masarray.github.io/ serta portfolio yang dideploy menunjuk release M3.H stable; README utama, Studio README, PROJECT_HANDOFF sesuai.
4. Source integration #295–#298 sudah punya status/strategi yang eksplisit; jangan diam-diam mengklaim sudah merged.
5. Penutup ke user disertai link GitHub Stable Release, Windows Setup, firmware, landing, file handoff; nilai status **100% hanya bila semua verifikasi DONE**.

## 4. Langkah kerja untuk thread baru (tanpa bertanya ulang)

1. **BACA FILE INI LENGKAP** dan cek state LIVE via GitHub + website (jangan mengandalkan state historis).
2. Verifikasi release `prerelease`, linked assets, `main` SHA, README, branch source, semua 3 landing.
3. **Selesaikan transisi Stable** terhadap release existing (pakai GitHub Actions workflow admin/auth bila perlu; jangan utak-atik firmware), verifikasi API.
4. Update wording Experimental/Pre-release pada README dan pages menjadi Stable *Lab Simulator*, deploy/smoke real URLs, berikan pembuktian link.
5. Rekonsiliasi dokumentasi roadmap dan PR status. Hindari merge ratusan commit jika belum aman; pisahkan menjadi pekerjaan integrasi yang terukur.
6. Akhiri dengan checklist status DONE/TODO faktual.

## 5. Prompt pembuka yang bisa langsung dipakai user

> LANJUTKAN HANDOFF ARSTACK61850 M3.H MENUJU 100% PUBLIC STABLE. Baca dahulu seluruh `docs/handoff/HANDOFF_ARSTACK61850_M3H_PUBLIC_STABLE_2026-10-24.md` di GitHub `masarray/arstack61850`. Jangan minta konfirmasi ulang. Release publik `processbus-sim-m3h-20261024` SUDAH terbit tetapi masih `prerelease=true`, belum stable. README dan source landing di `masarray/arstack61850`, `masarray/masarray.github.io`, serta `masarray/portfolio` SUDAH mengacu M3.H, tetapi verifikasi live/stable belum selesai. Kerjakan promotion release existing menjadi Stable/latest, sinkron wording README+landing, verifikasi deployment dan semua link, audit status source integration #295–#298. JANGAN mengubah source firmware M3.H `a922e9bdba4e57a4e88406eb83344455dd8ff142` atau mengklaim GPS/relay certified. Targetnya simulator IEC 61850 SV 4I+4V 4 kHz + PTP simulated Global GM, hardware timestamp untuk eksperimen engineer. Laporkan detail DONE/TODO berdasarkan bukti, serta link Stable Release, Setup, firmware, landing.

---
**Catatan:** status di dokumen ini adalah *snapshot faktual saat handoff* (24 Oktober 2026). State GitHub dan website dapat berubah; langkah pertama thread baru wajib memverifikasi ulang.
