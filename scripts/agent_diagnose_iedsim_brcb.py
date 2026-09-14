#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/static_ied_server.cpp")
text = path.read_text(encoding="utf-8")
old = '''            if (!brcb->reports->initialize()) {
                throw std::runtime_error("Could not initialize per-association BRCB runtime.");
            }
'''
new = '''            if (!brcb->reports->initialize()) {
                std::ostringstream detail;
                detail << "Could not initialize per-association BRCB runtime"
                       << " objectsValid=" << (process_objects->valid() ? "true" : "false")
                       << " dataSetsValid=" << (data_sets.valid() ? "true" : "false")
                       << " dataSetsAgainstObjects="
                       << (data_sets.valid_against(*process_objects) ? "true" : "false")
                       << " domain='" << definition.domain << "'"
                       << " item='" << definition.item << "'"
                       << " rptId='" << definition.report_id << "'"
                       << " dataSet='" << definition.data_set_domain << "/"
                       << definition.data_set_item << "'"
                       << " optFlds="
                       << static_cast<unsigned>(definition.optional_fields[0]) << ','
                       << static_cast<unsigned>(definition.optional_fields[1])
                       << " trgOps=" << static_cast<unsigned>(definition.trigger_options)
                       << " slots=" << brcb->slots.size()
                       << " slot0Bytes="
                       << (brcb->slots.empty() ? 0U : brcb->slots.front().storage.size())
                       << " dsMembers=" << brcb->data_set->members.size();
                throw std::runtime_error(detail.str());
            }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one BRCB initialize failure anchor, found {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Added invariant-level BRCB runtime initialization diagnostics.")
