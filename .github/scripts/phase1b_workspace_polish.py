from pathlib import Path


def patch(path, old, new, label):
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if old not in text:
        raise SystemExit(f'{label}: source block not found')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')

# Signal matrix: remove card-within-card chrome, tighten header and rows, keep all controls/logic intact.
patch('apps/arstack_studio/qml/SignalMatrix.qml',
'''    color: theme.surface2\n    radius: 8\n    border.width: 1\n    border.color: theme.lineSoft\n''',
'''    color: "#0b1219"\n    radius: 8\n    border.width: 0\n''', 'matrix outer surface')

patch('apps/arstack_studio/qml/SignalMatrix.qml',
'''            Layout.preferredHeight: 40\n            Layout.leftMargin: 12\n            Layout.rightMargin: 12\n            spacing: 8\n\n            Rectangle {\n                width: 3\n                height: 18\n                radius: 2\n                color: matrix.theme.accent\n            }\n''',
'''            Layout.preferredHeight: 36\n            Layout.leftMargin: 12\n            Layout.rightMargin: 12\n            spacing: 8\n''', 'matrix header density')

patch('apps/arstack_studio/qml/SignalMatrix.qml',
'''            Layout.preferredHeight: 25\n            Layout.leftMargin: 10\n            Layout.rightMargin: 10\n            spacing: 7\n''',
'''            Layout.preferredHeight: 22\n            Layout.leftMargin: 10\n            Layout.rightMargin: 10\n            spacing: 7\n''', 'matrix column header density')

patch('apps/arstack_studio/qml/SignalMatrix.qml',
'''                Layout.preferredHeight: matrix.compact ? 43 : 45\n                color: signalRow.selected ? "#101a24"\n                    : (magnitudeField.activeFocus || phaseField.activeFocus) ? "#111c27"\n                    : rowHover.hovered ? "#0f171f" : "transparent"\n''',
'''                Layout.preferredHeight: matrix.compact ? 42 : 44\n                color: signalRow.selected ? "#101d28"\n                    : (magnitudeField.activeFocus || phaseField.activeFocus) ? "#0f1922"\n                    : rowHover.hovered ? "#0d161e" : "transparent"\n''', 'matrix row density')

# Make the selected phase marker less visually aggressive.
patch('apps/arstack_studio/qml/SignalMatrix.qml',
'''                    width: 2\n                    anchors.left: parent.left\n''',
'''                    width: 2\n                    opacity: 0.82\n                    anchors.left: parent.left\n''', 'selected marker opacity')

# Workspace title and frequency strip: one compact hierarchy, fewer nested-card cues.
patch('apps/arstack_studio/qml/Main.qml',
'''                                Label { text: "4I + 4V Injection"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: root.compactLayout ? 18 : 20; font.weight: Font.DemiBold }\n''',
'''                                Label { text: "4I + 4V Injection"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: root.compactLayout ? 17 : 18; font.weight: Font.DemiBold }\n''', 'workspace title size')

patch('apps/arstack_studio/qml/Main.qml',
'''                            height: 53\n                            radius: 7\n                            color: studioTheme.surface2\n                            border.width: 1\n                            border.color: studioTheme.lineSoft\n''',
'''                            height: 48\n                            radius: 8\n                            color: "#0b1219"\n                            border.width: 0\n''', 'frequency strip surface')

patch('apps/arstack_studio/qml/Main.qml',
'''                                ColumnLayout {\n                                    spacing: 0\n                                    Label { text: root.signalFrequency === 0 ? "DC MODE" : "FREQUENCY"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: studioTheme.captionSize - 1; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }\n                                    RowLayout {\n                                        spacing: 5\n                                        NumericField {\n                                            id: frequencyField\n                                            theme: studioTheme\n                                            monoFont: root.monoFont\n                                            compact: root.compactLayout\n                                            implicitWidth: 84\n                                            text: "50.000"\n                                            suffixText: "Hz"\n                                            enabled: true\n                                            validator: DoubleValidator { bottom: 0.0; top: 1000.0; decimals: 3 }\n                                            onTextEdited: {\n                                                var value = root.parseOperatorNumber(text)\n                                                if (root.validFrequency(value)) {\n                                                    invalidInput = false\n                                                    root.signalFrequency = value\n                                                    if (value > 0) root.previousAcFrequency = value\n                                                    root.refreshPreview()\n                                                    if (workflowBar.session && workflowBar.session.liveControlReady)\n                                                        workflowBar.session.requestSetFrequency(value)\n                                                } else invalidInput = true\n                                            }\n                                            onEditingFinished: {\n                                                var value = root.parseOperatorNumber(text)\n                                                if (!root.validFrequency(value)) {\n                                                    text = root.signalFrequency.toFixed(3)\n                                                    invalidInput = false\n                                                    root.showMessage("Frequency must be within 0..1000 Hz (0 = DC).", true)\n                                                } else text = value.toFixed(3)\n                                            }\n                                        }\n                                    }\n                                }\n''',
'''                                Label {\n                                    text: "Frequency"\n                                    color: studioTheme.textSoft\n                                    font.family: root.uiFont\n                                    font.pixelSize: 10\n                                    font.weight: Font.DemiBold\n                                    verticalAlignment: Text.AlignVCenter\n                                }\n                                NumericField {\n                                    id: frequencyField\n                                    theme: studioTheme\n                                    monoFont: root.monoFont\n                                    compact: root.compactLayout\n                                    implicitWidth: 90\n                                    text: "50.000"\n                                    suffixText: "Hz"\n                                    enabled: true\n                                    validator: DoubleValidator { bottom: 0.0; top: 1000.0; decimals: 3 }\n                                    onTextEdited: {\n                                        var value = root.parseOperatorNumber(text)\n                                        if (root.validFrequency(value)) {\n                                            invalidInput = false\n                                            root.signalFrequency = value\n                                            if (value > 0) root.previousAcFrequency = value\n                                            root.refreshPreview()\n                                            if (workflowBar.session && workflowBar.session.liveControlReady)\n                                                workflowBar.session.requestSetFrequency(value)\n                                        } else invalidInput = true\n                                    }\n                                    onEditingFinished: {\n                                        var value = root.parseOperatorNumber(text)\n                                        if (!root.validFrequency(value)) {\n                                            text = root.signalFrequency.toFixed(3)\n                                            invalidInput = false\n                                            root.showMessage("Frequency must be within 0..1000 Hz (0 = DC).", true)\n                                        } else text = value.toFixed(3)\n                                    }\n                                }\n''', 'frequency horizontal hierarchy')

# Reduce outer workspace padding slightly and keep consistent 8px rhythm.
patch('apps/arstack_studio/qml/Main.qml',
'''                        anchors.margins: root.compactLayout ? 12 : 15\n                        spacing: 9\n''',
'''                        anchors.margins: root.compactLayout ? 11 : 13\n                        spacing: 8\n''', 'workspace rhythm')

print('Phase 1B workspace polish applied')
