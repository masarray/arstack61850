#!/usr/bin/env python3
"""Compatibility wrapper for tools/compare_live_model_json.py.

New automation should invoke tools/compare_live_model_json.py directly.  This
wrapper preserves the historical positional arguments and --output option used
by older arstack61850 evidence scripts while delegating all comparison semantics
to the single A1 structural comparator.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("expected", type=pathlib.Path, help="C# live model JSON")
    parser.add_argument("observed", type=pathlib.Path, help="C++ live model JSON")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--types", action="store_true")
    parser.add_argument("--runtime", action="store_true")
    arguments = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[1]
    comparator = root / "tools" / "compare_live_model_json.py"
    command = [
        sys.executable,
        str(comparator),
        str(arguments.expected),
        str(arguments.observed),
        "--json",
    ]
    if arguments.types:
        command.append("--types")
    if arguments.runtime:
        command.append("--runtime")

    process = subprocess.run(command, capture_output=True, text=True, check=False)
    if process.stderr:
        print(process.stderr, end="", file=sys.stderr)
    if process.stdout:
        if arguments.output:
            arguments.output.parent.mkdir(parents=True, exist_ok=True)
            arguments.output.write_text(process.stdout, encoding="utf-8")
        print(process.stdout, end="")
    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
