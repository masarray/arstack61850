#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one source-fix anchor, found {count}: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


controller = "apps/ied_simulator/src/IedSimulatorController.cpp"
replace_once(
    controller,
    "#include <filesystem>\n#include <set>\n",
    "#include <filesystem>\n#include <optional>\n#include <set>\n",
)
replace_once(
    controller,
    "        const auto key = domain + QLatin1Char('\n') + logicalNode + QLatin1Char('\n') + dataObject;\n",
    "        const auto key = domain + QLatin1Char('\\n') + logicalNode + QLatin1Char('\\n') + dataObject;\n",
)
replace_once(
    controller,
    '            QByteArray::number(*model) + "\n";\n',
    '            QByteArray::number(*model) + "\\n";\n',
)

regression = "apps/ied_simulator/test_gui_live_value.py"
replace_once(
    regression,
    '    return discovery.stdout.strip() + "\n" + rejected.stdout.strip() + "\n" + accepted.stdout.strip()\n',
    '    return discovery.stdout.strip() + "\\n" + rejected.stdout.strip() + "\\n" + accepted.stdout.strip()\n',
)

print("Configured-control C++/Python source syntax normalized.")
