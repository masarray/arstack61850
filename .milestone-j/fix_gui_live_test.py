#!/usr/bin/env python3
from pathlib import Path

path = Path("apps/ied_simulator/test_gui_live_value.py")
text = path.read_text(encoding="utf-8")
old = '''            if "kind=live_update_ack" not in app_output or "accepted=true" not in app_output:
                raise RuntimeError("GUI edit was not acknowledged by the live runtime data plane")
'''
new = '''            if "IEDSIM_LIVE_ACK generation=" not in app_output:
                raise RuntimeError("GUI edit was not acknowledged by the live runtime data plane")
'''
if old not in text:
    if new in text:
        print("GUI live ACK assertion already updated")
        raise SystemExit(0)
    raise SystemExit("GUI live ACK assertion anchor not found")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("apps/ied_simulator/test_gui_live_value.py: trust parsed live ACK evidence")
