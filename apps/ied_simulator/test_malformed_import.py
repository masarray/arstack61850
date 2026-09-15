#!/usr/bin/env python3
"""Prove malformed SCL fails closed through the interactive async importer."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    args = parser.parse_args()

    app = args.app.resolve()
    if not app.is_file():
        raise SystemExit(f"simulator executable not found: {app}")

    with tempfile.TemporaryDirectory(prefix="arstack-malformed-scl-") as temporary:
        path = Path(temporary) / "malformed.scd"
        path.write_text(
            "<?xml version=\"1.0\"?><SCL><IED name=\"BROKEN\"><AccessPoint>",
            encoding="utf-8",
        )
        env = os.environ.copy()
        env["QT_QPA_PLATFORM"] = "offscreen"
        completed = subprocess.run(
            [str(app), "--qa-async-import", str(path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env,
            timeout=15,
            check=False,
        )

    output = completed.stdout or ""
    if completed.returncode != 6:
        raise RuntimeError(
            "malformed SCL did not fail through the controlled importer path: "
            f"exit={completed.returncode} output={output[-4000:]}"
        )
    if "Async import failed:" not in output:
        raise RuntimeError(
            "malformed SCL failure was not surfaced to the QA control path: "
            f"output={output[-4000:]}"
        )
    if "ASYNC_IMPORT_OK" in output:
        raise RuntimeError("malformed SCL was incorrectly reported as a successful import")

    print("MALFORMED_IMPORT_PASS exit=6 fail_closed=true", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
