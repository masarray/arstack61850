#!/usr/bin/env python3
from pathlib import Path


path = Path("tools/static_ied_server.cpp")
text = path.read_text(encoding="utf-8")
changed = False

old_sizing = '''        mms::MmsStaticUrcbObjectBank sizing_bank{
            *urcb_runtime,
            object_table.objects(),
            std::span<mms::MmsStaticObjectEntry>{},
'''
new_sizing = '''        // Size the URCB object bank from the model that will actually be
        // wrapped. Configured controls may already have extended the base
        // object table with SBO/SBOw/Oper/Cancel objects. Sizing from the
        // original table under-allocates the per-association bank and makes
        // initialize() fail before the MMS association can be accepted.
        mms::MmsStaticUrcbObjectBank sizing_bank{
            *urcb_runtime,
            process_objects->objects(),
            std::span<mms::MmsStaticObjectEntry>{},
'''
if old_sizing in text:
    if text.count(old_sizing) != 1:
        raise SystemExit("expected exactly one legacy URCB sizing anchor")
    text = text.replace(old_sizing, new_sizing, 1)
    changed = True
elif new_sizing not in text:
    raise SystemExit("URCB sizing anchor not found in either legacy or repaired form")

old_table = '''        direct_control_table = std::make_unique<mms::MmsStaticObjectTable>(
            std::span<const mms::MmsStaticObjectEntry>{direct_control_objects});
'''
new_table = '''        // MmsStaticObjectTable requires strict (domain,item) ordering. The
        // structural model is already sorted, but control-service aliases are
        // appended above and can sort before ST/CF leaves (for example $CO$
        // sorts before $ST$). Re-sort the composed per-association table before
        // validating or exposing it to GetNameList/GVAA/Read/Write.
        std::stable_sort(
            direct_control_objects.begin(),
            direct_control_objects.end(),
            [](const mms::MmsStaticObjectEntry& left,
               const mms::MmsStaticObjectEntry& right) noexcept {
                if (left.domain != right.domain) return left.domain < right.domain;
                return left.item < right.item;
            });
        direct_control_table = std::make_unique<mms::MmsStaticObjectTable>(
            std::span<const mms::MmsStaticObjectEntry>{direct_control_objects});
'''
if new_table not in text:
    if text.count(old_table) != 1:
        raise SystemExit(
            f"expected one direct-control table anchor, found {text.count(old_table)}")
    text = text.replace(old_table, new_table, 1)
    changed = True

if changed:
    path.write_text(text, encoding="utf-8")
    print("Repaired control-object ordering and URCB composition for IED simulator associations.")
else:
    print("IED simulator control/URCB composition is already repaired.")
