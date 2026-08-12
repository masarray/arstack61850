from pathlib import Path

p = Path("tools/agent_p0_full_model_patch.py")
text = p.read_text(encoding="utf-8")

old = '''    return count > 1\\n        ? QStringLiteral("array:%1:%2").arg(count).arg(scalar)\\n        : scalar;\\n}\\n\\nnamespace detail {''')'''
new = '''    return count > 1\\n        ? QStringLiteral("array:%1:%2").arg(count).arg(scalar)\\n        : scalar;\\n}''')'''
if old not in text:
    raise SystemExit("namespace repair needle not found")
text = text.replace(old, new, 1)

start = '''replace_once(path,\n''' + "'''                QStringLiteral(\"path\"), QStringLiteral(\"rawType\"), QStringLiteral(\"type\"),\\n"
end = "new_manifest = r'''"
a = text.find(start)
if a < 0:
    raise SystemExit("metadata repair start not found")
b = text.find(end, a)
if b < 0:
    raise SystemExit("metadata repair end not found")
correct = '''replace_once(path,\n''' + "'''            QStringLiteral(\"rawType\"),\\n            QStringLiteral(\"iedName\"),''',\n'''            QStringLiteral(\"rawType\"),\\n            QStringLiteral(\"mmsTypeSignature\"),\\n            QStringLiteral(\"iedName\"),''')\n"
text = text[:a] + correct + text[b:]

p.write_text(text, encoding="utf-8")
print("P0 patcher repaired")
