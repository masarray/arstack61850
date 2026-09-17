from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if old not in text:
        raise SystemExit(f'{label}: expected source block not found')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')

# End-to-end command-bar height follows ModernRibbon's 54 px contract.
replace_once(
    'apps/arstack_studio/qml/WorkflowBar.qml',
    '    implicitHeight: 62\n    color: "transparent"\n',
    '    implicitHeight: 54\n    color: "transparent"\n',
    'WorkflowBar compact height')

replace_once(
    'apps/arstack_studio/qml/Main.qml',
    '            Layout.preferredHeight: 62\n            Layout.minimumHeight: 62\n',
    '            Layout.preferredHeight: 54\n            Layout.minimumHeight: 54\n',
    'Main command-bar height')

# Bottom bar becomes telemetry-only. Session state lives in one place: the command-bar chip.
main = Path('apps/arstack_studio/qml/Main.qml')
text = main.read_text(encoding='utf-8')
old = '''            Rectangle { width: 6; height: 6; radius: 3; color: workflowBar.smartStateColor }\n            Label {\n                text: device.deviceVerified ? device.portName + " · ESP32-P4" : workflowBar.displayState\n                color: device.deviceVerified ? studioTheme.textSoft : workflowBar.smartStateColor\n                font.family: root.uiFont\n                font.pixelSize: 9\n                font.weight: Font.Medium\n            }\n            Rectangle { width: 1; height: 14; color: studioTheme.lineSoft }\n            Label {\n                text: workflowBar.injectionRunning ? "SMV output active" : "4I + 4V · 4000 samples/s"\n                color: studioTheme.muted\n                font.family: root.uiFont\n                font.pixelSize: 9\n            }\n            Item { Layout.fillWidth: true }\n            Label {\n                visible: device.deviceVerified\n                text: device.running\n                    ? "FPS " + device.fps + "   MISSED " + device.missed + "   TX FAIL " + device.txFailures\n                    : "READY"\n                color: (Number(device.missed) > 0 || Number(device.txFailures) > 0) ? studioTheme.amber : studioTheme.textSoft\n                font.family: root.monoFont\n                font.pixelSize: 9\n                font.weight: Font.Medium\n            }\n'''
new = '''            Label {\n                text: device.deviceVerified ? device.portName + " · ESP32-P4" : "No device"\n                color: device.deviceVerified ? studioTheme.textSoft : studioTheme.muted\n                font.family: root.uiFont\n                font.pixelSize: 9\n                font.weight: Font.Medium\n            }\n            Rectangle { width: 1; height: 12; color: studioTheme.lineSoft }\n            Label {\n                text: "4I + 4V · 4000 samples/s"\n                color: studioTheme.muted\n                font.family: root.uiFont\n                font.pixelSize: 9\n            }\n            Item { Layout.fillWidth: true }\n            Label {\n                visible: device.deviceVerified\n                text: "FPS " + device.fps + "   MISSED " + device.missed + "   TX FAIL " + device.txFailures\n                color: (Number(device.missed) > 0 || Number(device.txFailures) > 0) ? studioTheme.amber : studioTheme.textSoft\n                font.family: root.monoFont\n                font.pixelSize: 9\n                font.weight: Font.Medium\n            }\n'''
if old not in text:
    raise SystemExit('Main telemetry footer block not found')
text = text.replace(old, new, 1)

# Remove decorative status words that do not add engineering information.
text = text.replace('                        statusText: "GENERATED"\n', '                        statusText: ""\n', 1)
main.write_text(text, encoding='utf-8')

print('Phase 1A premium shell patch applied')
