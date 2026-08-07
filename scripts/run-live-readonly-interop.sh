#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 ]]; then
  echo "Usage: $0 HOST [PORT] [OUTPUT_DIR] [CYCLES] [EXPECTED_CSHARP_MODEL]" >&2
  exit 2
fi
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARGS=("$ROOT/scripts/run-live-readonly-interop.py" "$1" "${2:-102}" --output "${3:-./interop-evidence}" --cycles "${4:-3}")
if [[ -n "${5:-}" ]]; then ARGS+=(--expected-csharp-model "$5"); fi
exec python3 "${ARGS[@]}"
