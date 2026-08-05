#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <capture.pcap> [build-directory]" >&2
  exit 64
fi

pcap=$1
build_dir=${2:-build-lab}

if [[ ! -f "$pcap" ]]; then
  echo "PCAP file not found: $pcap" >&2
  exit 66
fi

cmake -S . -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DARIEC61850_BUILD_TESTS=ON \
  -DARIEC61850_BUILD_TOOLS=ON \
  -DARIEC61850_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --parallel --target ariec61850_pcap_interop_check

report="${pcap%.*}.interop.json"
sha256sum "$pcap"
"$build_dir/ariec61850_pcap_interop_check" "$pcap" --json | tee "$report"
echo "Evidence report: $report"
