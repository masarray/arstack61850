#!/usr/bin/env python3
from pathlib import Path


path = Path("tools/static_ied_server.cpp")
text = path.read_text(encoding="utf-8")
old = '''        mms::MmsStaticUrcbObjectBank sizing_bank{
            *urcb_runtime,
            object_table.objects(),
            std::span<mms::MmsStaticObjectEntry>{},
'''
new = '''        // Size the URCB object bank from the model that will actually be
        // wrapped.  Configured controls may already have extended the base
        // object table with SBO/SBOw/Oper/Cancel objects.  Sizing from the
        // original table under-allocates the per-association bank and makes
        // initialize() fail before the MMS association can be accepted.
        mms::MmsStaticUrcbObjectBank sizing_bank{
            *urcb_runtime,
            process_objects->objects(),
            std::span<mms::MmsStaticObjectEntry>{},
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one URCB sizing anchor, found {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("IED simulator URCB sizing now composes from the active control-extended object table.")
