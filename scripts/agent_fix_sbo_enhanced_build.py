#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/static_ied_server.cpp")
text = path.read_text(encoding="utf-8")
old = '''[[nodiscard]] bool apply_atomic_boolean(void* context, const bool value) noexcept {
    if (context == nullptr) return false;
    static_cast<std::atomic<std::uint8_t>*>(context)->store(
        value ? 1U : 0U, std::memory_order_relaxed);
    return true;
}

'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"static_ied_server.cpp: expected one obsolete apply_atomic_boolean helper, found {count}")
path.write_text(text.replace(old, "", 1), encoding="utf-8")
print("Removed obsolete Direct-Normal apply callback after shared control-state migration.")
