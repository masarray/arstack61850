#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/static_ied_server.cpp")
text = path.read_text(encoding="utf-8")
old = "        constexpr std::array<std::uint8_t, 2U> unsigned_type{0x86U, 0x00U};\n"
new = "        static constexpr std::array<std::uint8_t, 2U> unsigned_type{0x86U, 0x00U};\n"
if old in text:
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one ctlModel type-storage anchor, found {text.count(old)}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("Pinned ctlModel MMS type specification to static lifetime across URCB/BRCB composition.")
elif new in text:
    print("ctlModel MMS type specification already has static lifetime.")
else:
    raise SystemExit("ctlModel type-storage anchor not found")
