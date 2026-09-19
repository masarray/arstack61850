#!/usr/bin/env python3
from pathlib import Path

path = Path("apps/ied_simulator/src/IedSimulatorController.cpp")
text = path.read_text(encoding="utf-8")

old_include = "#include <QDateTime>\n#include <QDir>\n"
new_include = "#include <QDateTime>\n#include <QDebug>\n#include <QDir>\n"
if text.count(old_include) != 1:
    raise SystemExit("expected one Qt include anchor")
text = text.replace(old_include, new_include, 1)

old = '''void IedSimulatorController::processServerLine(
    const QString& line,
    const bool standardError) {
    if (!line.startsWith(QStringLiteral("IEDSIM_EVENT "))) {
'''
new = '''void IedSimulatorController::processServerLine(
    const QString& line,
    const bool standardError) {
    // The GUI normally turns child-server protocol events into activity rows.
    // For deterministic CI/interoperability diagnostics, opt in to mirroring
    // those exact child lines to the parent process log.  This keeps normal UI
    // output quiet while making association failures observable in headless QA.
    if (qEnvironmentVariableIsSet("ARSTACK_IEDSIM_TRACE_SERVER")) {
        if (standardError) {
            qWarning().noquote() << line;
        } else {
            qInfo().noquote() << line;
        }
    }
    if (!line.startsWith(QStringLiteral("IEDSIM_EVENT "))) {
'''
if text.count(old) != 1:
    raise SystemExit("expected one processServerLine anchor")
text = text.replace(old, new, 1)
path.write_text(text, encoding="utf-8")
print("Enabled opt-in child MMS server trace forwarding for headless regression diagnostics.")
