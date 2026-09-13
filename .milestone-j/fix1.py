#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, got {count}: {old[:80]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"{path}: updated")


replace_once(
    "tools/static_ied_server.cpp",
    "        const auto data = mms::MmsSimulatorManifestCodec::data(\n"
    "            value.type, value.raw_type, value.normalized_type, update.value);\n"
    "        if (!data.has_value()) continue;\n"
    "        value.text = update.value;\n"
    "        value.data = data;\n",
    "        const auto data = mms::MmsSimulatorManifestCodec::data(\n"
    "            value.type, value.raw_type, value.normalized_type, update.value);\n"
    "        value.text = update.value;\n"
    "        value.data = data;\n",
)

replace_once(
    "tools/static_ied_server.cpp",
    "        const auto parsed = mms::MmsSimulatorManifestCodec::data(\n"
    "            target.type, target.raw_type, target.normalized_type, *value);\n"
    "        if (!parsed.has_value()) {\n"
    "            reject_live_update(generation, revision, \"invalid-value\");\n"
    "            continue;\n"
    "        }\n\n"
    "        LiveUpdate update;\n",
    "        const auto parsed = mms::MmsSimulatorManifestCodec::data(\n"
    "            target.type, target.raw_type, target.normalized_type, *value);\n\n"
    "        LiveUpdate update;\n",
)

replace_once(
    "apps/ied_simulator/src/IedFleetController.cpp",
    "    sending.reserve(std::min(kLiveFlushBudget, candidates.size()));\n",
    "    sending.reserve(std::min(kLiveFlushBudget, static_cast<int>(candidates.size())));\n",
)
