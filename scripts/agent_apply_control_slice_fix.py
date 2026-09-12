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
p.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Adjusted URCB/BRCB runtime anchors in patch driver.")
