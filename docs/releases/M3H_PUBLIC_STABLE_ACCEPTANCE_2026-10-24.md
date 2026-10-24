# ARStack61850 M3.H — Public Stable Acceptance Evidence
**Evidence date:** 24 October 2026 (Asia/Jakarta)
**Scope:** free/public isolated-lab IEC 61850 Process Bus SV + simulated Global GM PTP emulator. Stable means the shipped package and its stated local evidence are reproducible; not certified UTC/GPS or proven IED lock.

## Release identity (DONE)

- Existing GitHub Release [processbus-sim-m3h-20261024](https://github.com/masarray/arstack61850/releases/tag/processbus-sim-m3h-20261024), ID **422544367**, promoted **in place**. GitHub API returns `draft=false`, `prerelease=false`, and `/releases/latest` points to the same release. [Promotion verification run](https://github.com/masarray/arstack61850/actions/runs/40071700074): SUCCESS; [release title/body synchronization run](https://github.com/masarray/arstack61850/actions/runs/40072826843): SUCCESS.
- Shipped source **a922e9bdba4e57a4e88406eb83344455dd8ff142**, firmware Build ID **a922e9bdba4e57a4**, 10/10 source-head CI SUCCESS. Release target `a4b6e53d0dbbcababc55ba8a4ff97ade6f2a6200` is the documentation/packaging tag, not a changed firmware payload.
- **All five assets unchanged** after promotion, verified by GitHub release asset IDs/sizes/digests AND real HTTP downloads.

| Asset | Size bytes | SHA-256 |
| --- | ---: | --- |
| `ARStack-ProcessBus-M3H-win-x64-setup.exe` | 31,059,935 | `4902e8bf44e78fc55a139ce84a1273f0768ef6fe10000f5d0f85d75d2f6c736e` |
| `ARStack-ProcessBus-M3H-win-x64-portable.zip` | 48,335,087 | `ca01424e4102aa65e353737024ebf3258b4d2f614fa60d932c95c353ee228d8f` |
| `ARStack-ESP32P4-ProcessBus-M3H-firmware.bin` | 542,096 | `61c508e9bf1357eb964d2b817b260b6a0e985446ddd68de1d45d9467e25c4607` |
| `firmware-manifest.json` | 942 | `b2878148a1cda30f11a4c93a200bcba07bb63548aa571de8d05260c2ef630f9a` |
| `SHA256SUMS.txt` | 416 | `c7674d1a97b77b66adda12216f7d5e460f86da5cb07fd1d1f02bf3011b99c876` |

## On-wire / CI evidence boundaries (DONE, scoped)

- M3.H independent PCAP: 40.762934 s, 163,048 SV, 3999.884 fps mean, 100% `smpSynch=2`, no missing/duplicate/reordered/malformed SV in valid segment; 44 PTP Announce, 43 Sync, 43 paired Follow_Up.
- STOP truncated the attempted 120 s capture; **do not claim 120 s endurance**. No connected relay acceptance, Pdelay peer evidence or physical GPS/UTC traceability is proven. Not blockers for an engineering simulator.
- The earlier experimental-release workflow has been turned into **manual read-only verification**, removing previous `--clobber` and `--prerelease` downgrade behavior; it cannot overwrite this Stable release.

## Public web and downloads (DONE)

1. Product URL [https://masarray.github.io/arstack61850/](https://masarray.github.io/arstack61850/) HTTP 200, `/arstack61850/index.html` HTTP 200.
2. Portal URL [https://masarray.github.io/](https://masarray.github.io/) HTTP 200 with Public Stable product copy.
3. Project Pages ownership fixed: the `masarray/arstack61850` repository has GitHub Pages enabled and claims the `/arstack61850/` route. A file solely at `masarray/masarray.github.io/arstack61850/index.html` returned HTTP 404, despite portal Pages reporting SUCCESS. Created the product entry point at `masarray/arstack61850/index.html` and published with [owning project Pages run](https://github.com/masarray/arstack61850/actions/runs/40072480932), SUCCESS.
4. [Live public HTTP and download acceptance](https://github.com/masarray/masarray.github.io/actions/runs/40072546814): **SUCCESS** on GitHub-hosted runner: product + portal 200, stable HTML/metadata, five release-asset URLs in live DOM, real full HTTP download of all five assets, `sha256sum --check`, independent pinned SHA check, ZIP CRC, manifest Build ID/source SHA, embedded firmware byte-for-byte equal to separate BIN.

## Documentation and source integration (DONE; main contains the verified shipped source and release tag)

- Updated `README.md`, `apps/arstack_studio/README.md`, `PROJECT_HANDOFF.md`, `docs/releases/PROCESS_BUS_SIMULATOR_M3H_PUBLIC_20261024.md`, product landing, MasArray portal and `masarray/portfolio/src/data/portfolio.ts`. Stable means lab simulator, never physical GPS/IED certification.
- Detailed [M3.H source stack audit](../integration/M3H_SOURCE_STACK_AUDIT_2026-10-24.md): verified source `a922e9bd...` and release tag `a4b6e53d...` are now Git ancestors of `main` through [merged PR #299](https://github.com/masarray/arstack61850/pull/299), main commit `ad2ebd0e7696dcbc389f1241fedbec5ecf504881`, after **14/14 CI SUCCESS** on exact integration head `c90a979...`. Eight stacked ancestor PRs #290–#293/#295–#298 were closed only after GitHub compare confirmed each HEAD was contained in main (behind=0). Divergent branches remain independent.
- GitHub Actions [portfolio build](https://github.com/masarray/portfolio/actions/runs/40071896101) SUCCESS after the portfolio source wording update.

## Remaining release-closure boundaries — do not conflate with Stable package

- **Portfolio live URL/deployment: NOT VERIFIED.** `masarray/portfolio` has `has_pages=false`, no GitHub Pages publication and no verifiable deployment URL in its GitHub homepage metadata. This does **not** prove an external Lovable deployment is absent; it means no deployed portfolio page was actually confirmed. Source sync/build pass are not equivalent to live deployment.
- **Rendered browser acceptance: DONE.** [Playwright live acceptance run 40072896355](https://github.com/masarray/arstack61850/actions/runs/40072896355) **SUCCESS** at 1440, 768, 390 and 360 px widths; all detected Public Stable copy, tested Build ID, CTA and OpenGraph, with no horizontal overflow. [Screenshot artifact](https://github.com/masarray/arstack61850/actions/runs/40072896355/artifacts/12552515189) contains four actual rendered screenshots.
- Source stack integration **completed** with matched source identity and main preservation gates. Separate *future* development of external GM/relay interoperability, 4800 fps, and advanced SV profile variants remains open but is not part of M3.H Stable simulator claims.

**Completion rule:** The GitHub release, package identity, live product + portal, and all five real downloads have passed. Do **not** report 100% of the original all-surfaces mandate until the externally deployed portfolio is identified/verified and the browser-responsive gate has been closed.
