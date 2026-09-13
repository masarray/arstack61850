#!/usr/bin/env python3
from pathlib import Path

server = Path("tools/static_ied_server.cpp")
text = server.read_text(encoding="utf-8")

old = '''        const auto data = mms::MmsSimulatorManifestCodec::data(
            value.type, value.raw_type, value.normalized_type, update.value);
        if (!data.has_value()) continue;
        value.text = update.value;
        value.data = data;
        value.encoded = mms::MmsDataCodec::encode(*value.data);
'''
new = '''        const auto data = mms::MmsSimulatorManifestCodec::data(
            value.type, value.raw_type, value.normalized_type, update.value);
        value.text = update.value;
        value.data = data;
        value.encoded = mms::MmsDataCodec::encode(*value.data);
'''
if old not in text:
    raise SystemExit("apply_live_updates compile-fix anchor not found")
text = text.replace(old, new, 1)

old = '''        const auto parsed = mms::MmsSimulatorManifestCodec::data(
            target.type, target.raw_type, target.normalized_type, *value);
        if (!parsed.has_value()) {
            reject_live_update(generation, revision, "invalid-value");
            continue;
        }

        LiveUpdate update;
'''
new = '''        const auto parsed = mms::MmsSimulatorManifestCodec::data(
            target.type, target.raw_type, target.normalized_type, *value);

        LiveUpdate update;
'''
if old not in text:
    raise SystemExit("drain_live_stdin compile-fix anchor not found")
text = text.replace(old, new, 1)
server.write_text(text, encoding="utf-8")
print("tools/static_ied_server.cpp: fixed MmsDataValue usage")

controller = Path("apps/ied_simulator/src/IedFleetController.cpp")
text = controller.read_text(encoding="utf-8")
old = "    sending.reserve(std::min(kLiveFlushBudget, candidates.size()));\n"
new = "    sending.reserve(std::min(kLiveFlushBudget, static_cast<int>(candidates.size())));\n"
if old not in text:
    raise SystemExit("live flush reserve compile-fix anchor not found")
controller.write_text(text.replace(old, new, 1), encoding="utf-8")
print("apps/ied_simulator/src/IedFleetController.cpp: fixed Qt size type usage")
