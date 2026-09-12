#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {count}: {old[:140]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "tests/test_simulation.cpp",
    "#include <stdexcept>\n#include <string>\n",
    "#include <stdexcept>\n#include <string>\n#include <utility>\n",
)

replace_once(
    "MIGRATION_CHECKLIST.md",
    "## Phase 5 — simulator and applications\n\n"
    "- [ ] Deterministic IED simulation and read-only MMS server integration parity.\n",
    "## Phase 5 — simulator and applications\n\n"
    "### Phase 5A — native deterministic simulator domain runtime\n\n"
    "- [x] Native `IedSimulatorProfile` domain model ported from the C# simulation surface.\n"
    "- [x] Native SCL-to-simulator profile builder uses the complete structural `DataTypeTemplates` projection; DataSets enrich but never shrink the model.\n"
    "- [x] Selected/runtime IED identity remap, structural-only leaves, DataSets, URCB/BRCB metadata, configured `ctlModel`, Quality, and Timestamp defaults are regression-tested.\n"
    "- [x] Deterministic `IedSimulatorEngine` reset/start/stop/manual mutation/step/snapshot state model ported without wall-clock dependencies in the core.\n"
    "- [x] Qt simulator value presentation consumes the portable profile builder instead of maintaining a second leaf-discovery/default-classification implementation.\n"
    "- [x] GCC strict regression plus Qt public-wire simulator regression guards the ported runtime adapter.\n\n"
    "- [ ] Deterministic IED simulation and read-only MMS server integration parity.\n",
)

print("Simulator Phase 5A milestone metadata staged.")
