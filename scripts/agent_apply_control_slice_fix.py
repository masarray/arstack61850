#!/usr/bin/env python3
from pathlib import Path

p = Path("scripts/agent_apply_control_slice.py")
text = p.read_text(encoding="utf-8")
old = '''needle = "            object_table,\\n            data_sets);"
if server.count(needle) != 2:
    raise SystemExit(
        "static_ied_server.cpp: expected two report runtime object_table anchors, "
        f"found {server.count(needle)}"
    )
server = server.replace(needle, "            *process_objects,\\n            data_sets);", 2)
'''
new = '''urcb_runtime_anchor = "            object_table,\\n            data_sets);"
if server.count(urcb_runtime_anchor) != 1:
    raise SystemExit(
        "static_ied_server.cpp: URCB runtime object_table anchor mismatch: "
        f"{server.count(urcb_runtime_anchor)}"
    )
server = server.replace(
    urcb_runtime_anchor,
    "            *process_objects,\\n            data_sets);",
    1,
)
brcb_runtime_anchor = "                object_table,\\n                data_sets);"
if server.count(brcb_runtime_anchor) != 1:
    raise SystemExit(
        "static_ied_server.cpp: BRCB runtime object_table anchor mismatch: "
        f"{server.count(brcb_runtime_anchor)}"
    )
server = server.replace(
    brcb_runtime_anchor,
    "                *process_objects,\\n                data_sets);",
    1,
)
'''
if text.count(old) != 1:
    raise SystemExit(f"control patch driver anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)

marker = "# 2) Extend the E2E fixture with one SCL-configured SPC control.\n"
if text.count(marker) != 1:
    raise SystemExit(f"control patch driver parser marker mismatch: {text.count(marker)}")
aggregate_patch = '''# Keep strict -Wmissing-field-initializers builds explicit for the new field.
replace_once(
    "src/scl/parser_part_04.inc",
    "        is_quality_attribute(da_name),\\n"
    "        is_timestamp_attribute(da_name),\\n"
    "    };\\n",
    "        is_quality_attribute(da_name),\\n"
    "        is_timestamp_attribute(da_name),\\n"
    "        {},\\n"
    "    };\\n",
)
replace_once(
    "src/scl/parser_part_04.inc",
    "            is_quality_attribute(leaf.da_name),\\n"
    "            is_timestamp_attribute(leaf.da_name),\\n"
    "        });\\n",
    "            is_quality_attribute(leaf.da_name),\\n"
    "            is_timestamp_attribute(leaf.da_name),\\n"
    "            {},\\n"
    "        });\\n",
)

'''
text = text.replace(marker, aggregate_patch + marker, 1)
p.write_text(text, encoding="utf-8")
print("Adjusted report-runtime anchors and strict SCL aggregate initializers in patch driver.")
