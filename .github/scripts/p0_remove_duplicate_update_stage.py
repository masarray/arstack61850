from pathlib import Path
p = Path('apps/arstack_studio/src/SmartSessionController.hpp')
text = p.read_text(encoding='utf-8')
old = '''    enum class UpdateStage {\n        idle,\n        stopping,\n        releasingPort,\n        probing,\n        flashing,\n        reconnecting,\n        waitingForBootloader,\n    };\n    enum class ProfileSyncStage { idle, deploying, failed };\n'''
new = '''    enum class ProfileSyncStage { idle, deploying, failed };\n'''
# Remove only the private duplicate. Public declaration must remain.
if text.count(old) != 1:
    raise SystemExit(f'expected exactly one private duplicate block, found {text.count(old)}')
text = text.replace(old, new, 1)
if text.count('enum class UpdateStage {') != 1:
    raise SystemExit('public UpdateStage declaration invariant failed')
p.write_text(text, encoding='utf-8')
print('private duplicate UpdateStage removed; public supervisor enum retained')
