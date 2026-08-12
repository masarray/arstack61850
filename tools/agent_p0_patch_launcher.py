from pathlib import Path

source = Path("tools/agent_p0_full_model_patch.py").read_text(encoding="utf-8")

# The materializer helper must return to arstack::iedsim before the public
# materializeSclMmsModel entry point. Remove the accidental detail reopen from
# the generated replacement text.
needle = r"\n\nnamespace detail {''')"
if needle not in source:
    raise SystemExit("launcher: detail namespace needle not found")
source = source.replace(needle, "''')", 1)

# Replace the one stale metadata-list guard with the actual controller list.
bad_anchor = "replace_once(path,\n'''                QStringLiteral(\"path\"), QStringLiteral(\"rawType\"), QStringLiteral(\"type\"),"
a = source.find(bad_anchor)
if a < 0:
    raise SystemExit("launcher: stale metadata guard not found")
b = source.find("new_manifest = r'''", a)
if b < 0:
    raise SystemExit("launcher: new_manifest marker not found")
correct = '''replace_once(path,\n''' + "'''            QStringLiteral(\"rawType\"),\\n            QStringLiteral(\"iedName\"),''',\n'''            QStringLiteral(\"rawType\"),\\n            QStringLiteral(\"mmsTypeSignature\"),\\n            QStringLiteral(\"iedName\"),''')\n"
source = source[:a] + correct + source[b:]

exec(compile(source, "agent_p0_full_model_patch.py", "exec"), {"__name__": "__main__"})
