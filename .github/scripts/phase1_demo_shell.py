from pathlib import Path


def remove_qml_object(text: str, marker: str) -> str:
    start = text.find(marker)
    if start < 0:
        raise SystemExit(f'marker not found: {marker!r}')
    brace = text.find('{', start)
    if brace < 0:
        raise SystemExit(f'opening brace not found: {marker!r}')
    depth = 0
    in_string = False
    escaped = False
    i = brace
    while i < len(text):
        ch = text[i]
        if in_string:
            if escaped:
                escaped = False
            elif ch == '\\':
                escaped = True
            elif ch == '"':
                in_string = False
        else:
            if ch == '"':
                in_string = True
            elif ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
                if depth == 0:
                    end = i + 1
                    while end < len(text) and text[end] in ' \t':
                        end += 1
                    if end < len(text) and text[end] == '\n':
                        end += 1
                    return text[:start] + text[end:]
        i += 1
    raise SystemExit(f'unbalanced QML object: {marker!r}')


main_path = Path('apps/arstack_studio/qml/Main.qml')
main = main_path.read_text(encoding='utf-8')

# Remove the legacy brand/status header. The command bar now owns the entire
# application chrome below the native window title bar.
main = remove_qml_object(main, '    header: Rectangle {')

# Replace the verbose footer with one compact connection/telemetry strip.
footer_start = main.find('    footer: Rectangle {')
if footer_start < 0:
    raise SystemExit('footer block not found')
footer_removed = remove_qml_object(main, '    footer: Rectangle {')
insert_at = footer_removed.find('    Window {\n        id: configurationWindow')
if insert_at < 0:
    raise SystemExit('configuration window anchor not found')
footer = '''    footer: Rectangle {
        height: 28
        color: studioTheme.chrome
        border.width: 1
        border.color: studioTheme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            Rectangle { width: 6; height: 6; radius: 3; color: workflowBar.smartStateColor }
            Label {
                text: device.deviceVerified ? device.portName + " · ESP32-P4" : workflowBar.displayState
                color: device.deviceVerified ? studioTheme.textSoft : workflowBar.smartStateColor
                font.family: root.uiFont
                font.pixelSize: 9
                font.weight: Font.Medium
            }
            Rectangle { width: 1; height: 14; color: studioTheme.lineSoft }
            Label {
                text: workflowBar.injectionRunning ? "SMV output active" : "4I + 4V · 4000 samples/s"
                color: studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: 9
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: device.deviceVerified
                text: device.running
                    ? "FPS " + device.fps + "   MISSED " + device.missed + "   TX FAIL " + device.txFailures
                    : "READY"
                color: (Number(device.missed) > 0 || Number(device.txFailures) > 0) ? studioTheme.amber : studioTheme.textSoft
                font.family: root.monoFont
                font.pixelSize: 9
                font.weight: Font.Medium
            }
        }
    }

'''
main = footer_removed[:insert_at] + footer + footer_removed[insert_at:]

main = main.replace(
    '        anchors.margins: root.compactLayout ? 9 : 11\n        spacing: root.compactLayout ? 9 : 11\n',
    '        anchors.margins: 8\n        spacing: 8\n',
    1)

# One command bar, fixed instrument density.
main = main.replace(
    '            Layout.preferredHeight: workflowBar.implicitHeight\n            Layout.minimumHeight: workflowBar.implicitHeight\n',
    '            Layout.preferredHeight: 62\n            Layout.minimumHeight: 62\n',
    1)

# Remove redundant eyebrow copy above the injection title.
noise = '                                Label { text: "INJECTION"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: studioTheme.captionSize; font.weight: Font.DemiBold; font.letterSpacing: 0.9 }\n'
if noise not in main:
    raise SystemExit('INJECTION eyebrow not found')
main = main.replace(noise, '', 1)

# The monitor becomes a deliberate drawer instead of a permanent collapsed bar.
main = main.replace(
    '                visible: root.telemetryDockVisible\n',
    '                visible: root.telemetryDockVisible && root.telemetryExpanded\n',
    1)
main = main.replace('        anchors.bottomMargin: 48\n', '        anchors.bottomMargin: 38\n', 1)
main_path.write_text(main, encoding='utf-8')

workflow_path = Path('apps/arstack_studio/qml/WorkflowBar.qml')
workflow = workflow_path.read_text(encoding='utf-8')
old = '    implicitHeight: ribbon.compact ? 70 : 96\n'
if workflow.count(old) != 1:
    raise SystemExit(f'expected one legacy workflow height, got {workflow.count(old)}')
workflow = workflow.replace(old, '    implicitHeight: 62\n', 1)
workflow_path.write_text(workflow, encoding='utf-8')

print('Phase 1 demo shell patch applied')
