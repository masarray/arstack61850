#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/static_ied_server.cpp")
text = path.read_text(encoding="utf-8")
changed = False

old_capture = '''            worker->thread = std::jthread([\n                &options,\n                &manifest_type,\n                &manifest_value,\n                &object_table,\n'''
new_capture = '''            worker->thread = std::jthread([\n                &options,\n                &manifest_type,\n                &manifest_value,\n                &manifest_model,\n                &object_table,\n'''
if old_capture in text:
    if text.count(old_capture) != 1:
        raise SystemExit("expected exactly one worker capture anchor")
    text = text.replace(old_capture, new_capture, 1)
    changed = True
elif new_capture not in text:
    raise SystemExit("worker capture anchor not found")

old_load = '''                        auto local_model = load_manifest_model(\n                            options.model_manifest, manifest_type, manifest_value);\n                        const auto local_object_span =\n'''
new_load = '''                        auto local_model = load_manifest_model(\n                            options.model_manifest, manifest_type, manifest_value);\n\n                        // Process state belongs to the simulated IED, not to an MMS\n                        // association. Per-association model copies isolate mutable\n                        // report/control bookkeeping, but every configured control\n                        // must point at the canonical server-level process/selection\n                        // state so an Oper performed by one client is immediately\n                        // visible to a second client and SBO ownership is global.\n                        for (auto& local_control : local_model.direct_control_storage) {\n                            const auto shared = std::find_if(\n                                manifest_model.direct_control_storage.begin(),\n                                manifest_model.direct_control_storage.end(),\n                                [&](const auto& candidate) {\n                                    return candidate.domain == local_control.domain &&\n                                        candidate.status_item == local_control.status_item &&\n                                        candidate.control_model == local_control.control_model;\n                                });\n                            if (shared == manifest_model.direct_control_storage.end() ||\n                                shared->shared_state == nullptr) {\n                                throw std::runtime_error(\n                                    \"Per-association control has no canonical shared state.\");\n                            }\n                            local_control.shared_state = shared->shared_state;\n                        }\n\n                        const auto local_object_span =\n'''
if old_load in text:
    if text.count(old_load) != 1:
        raise SystemExit("expected exactly one per-association model-load anchor")
    text = text.replace(old_load, new_load, 1)
    changed = True
elif new_load not in text:
    raise SystemExit("per-association model-load anchor not found")

if changed:
    path.write_text(text, encoding="utf-8")
    print("Bound per-association controls to canonical simulated-process shared state.")
else:
    print("Cross-association control shared state is already bound.")
