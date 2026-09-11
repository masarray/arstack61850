// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: matrix
    property var theme
    property var controller
    property var device
    property var sourceModel
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property int groupIndex: 0
    property string titleText: "Current"
    property string symbolText: "I"
    property string unitText: "A RMS"
    property bool compact: false

    color: theme.surface2
    radius: 9
    border.width: 1
    border.color: theme.lineSoft

    function phaseColorFor(index) {
        return [theme.phaseA, theme.phaseB, theme.phaseC, theme.phaseN][index]
    }

    function focusCell(row, column) {
        var rowItem = rowRepeater.itemAt(row)
        if (!rowItem)
            return
        var editor = column === 0 ? rowItem.magnitudeEditor : rowItem.phaseEditor
        editor.forceActiveFocus()
        editor.selectAll()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 9

            Rectangle {
                width: 23
                height: 23
                radius: 6
                color: "#122033"
                border.width: 1
                border.color: "#223955"
                Label {
                    anchors.centerIn: parent
                    text: matrix.symbolText
                    color: matrix.theme.accent
                    font.family: matrix.monoFont
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }
            Label {
                text: matrix.titleText
                color: matrix.theme.text
                font.family: matrix.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            Label {
                text: matrix.unitText
                color: matrix.theme.muted
                font.family: matrix.uiFont
                font.pixelSize: 10
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: matrix.theme.lineSoft }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 8
            Item { Layout.preferredWidth: 28 }
            Label { text: "Channel"; Layout.preferredWidth: 46; color: matrix.theme.muted; font.family: matrix.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
            Label { text: "Value"; Layout.fillWidth: true; color: matrix.theme.muted; font.family: matrix.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
            Label { text: "Phase"; Layout.preferredWidth: matrix.compact ? 92 : 108; color: matrix.theme.muted; font.family: matrix.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
        }

        Repeater {
            id: rowRepeater
            model: matrix.sourceModel

            delegate: Rectangle {
                id: signalRow
                property int rowIndex: index
                property string sid: model.signalId
                property real mag: model.magnitude
                property real angle: model.phase
                property bool channelEnabled: model.enabled
                property real signalQuality: model.quality
                property color phaseColor: matrix.phaseColorFor(rowIndex)
                property bool selected: matrix.controller.activeSignal === signalRow.sid
                property alias magnitudeEditor: magnitudeField
                property alias phaseEditor: phaseField
                property real pendingMagnitude: mag
                property real pendingPhase: angle

                Layout.fillWidth: true
                Layout.preferredHeight: matrix.compact ? 54 : 60
                color: signalRow.selected ? "#111c27"
                    : (magnitudeField.activeFocus || phaseField.activeFocus) ? "#131f2b"
                    : rowHover.hovered ? "#101820" : "transparent"
                Behavior on color { ColorAnimation { duration: 90 } }

                HoverHandler { id: rowHover }

                Timer {
                    id: magnitudeApplyTimer
                    interval: 90
                    repeat: false
                    onTriggered: matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "magnitude", signalRow.pendingMagnitude)
                }
                Timer {
                    id: phaseApplyTimer
                    interval: 90
                    repeat: false
                    onTriggered: matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "phase", signalRow.pendingPhase)
                }

                onMagChanged: if (!magnitudeField.activeFocus) magnitudeField.text = mag.toFixed(3)
                onAngleChanged: if (!phaseField.activeFocus) phaseField.text = angle.toFixed(2)

                Rectangle {
                    visible: signalRow.selected
                    width: 3
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: signalRow.phaseColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    CheckBox {
                        Layout.preferredWidth: 28
                        Layout.alignment: Qt.AlignVCenter
                        checked: signalRow.channelEnabled
                        ToolTip.visible: hovered
                        ToolTip.text: checked ? "Channel enabled" : "Channel disabled"
                        onToggled: {
                            matrix.sourceModel.setProperty(signalRow.rowIndex, "enabled", checked)
                            matrix.controller.selectSignal(matrix.groupIndex, signalRow.rowIndex)
                            matrix.controller.refreshPreview()
                            if (matrix.device.deviceVerified)
                                matrix.device.setEnabled(signalRow.sid, checked)
                        }
                    }

                    RowLayout {
                        Layout.preferredWidth: 46
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 6
                        Rectangle { width: 8; height: 8; radius: 4; color: signalRow.phaseColor }
                        Label {
                            text: signalRow.sid
                            color: signalRow.selected ? matrix.theme.text : matrix.theme.textSoft
                            font.family: matrix.monoFont
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    NumericField {
                        id: magnitudeField
                        Layout.fillWidth: true
                        theme: matrix.theme
                        monoFont: matrix.monoFont
                        compact: matrix.compact
                        suffixText: matrix.groupIndex === 0 ? "A" : "V"
                        text: signalRow.mag.toFixed(3)
                        invalidInput: false
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                matrix.controller.selectSignal(matrix.groupIndex, signalRow.rowIndex)
                                text = signalRow.mag.toFixed(3)
                                selectAll()
                            }
                        }
                        onTextEdited: {
                            var value = matrix.controller.parseOperatorNumber(text)
                            if (matrix.controller.validMagnitude(matrix.groupIndex, value)) {
                                invalidInput = false
                                signalRow.pendingMagnitude = value
                                magnitudeApplyTimer.restart()
                            } else {
                                invalidInput = true
                            }
                        }
                        onEditingFinished: {
                            var value = matrix.controller.parseOperatorNumber(text)
                            if (!matrix.controller.validMagnitude(matrix.groupIndex, value)) {
                                magnitudeApplyTimer.stop()
                                text = signalRow.mag.toFixed(3)
                                invalidInput = false
                                matrix.controller.showMessage(signalRow.sid + " value is outside the supported range.", true)
                            } else {
                                signalRow.pendingMagnitude = value
                                magnitudeApplyTimer.stop()
                                matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "magnitude", value)
                                text = value.toFixed(3)
                            }
                        }
                        Keys.onPressed: function(event) {
                            if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0) {
                                matrix.controller.navigate(matrix.groupIndex, signalRow.rowIndex, 0, event.key)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                matrix.controller.focusCell(matrix.groupIndex, signalRow.rowIndex + 1, 0)
                                event.accepted = true
                            }
                        }
                    }

                    NumericField {
                        id: phaseField
                        enabled: matrix.controller.signalFrequency > 0
                        Layout.preferredWidth: matrix.compact ? 92 : 108
                        theme: matrix.theme
                        monoFont: matrix.monoFont
                        compact: matrix.compact
                        suffixText: "°"
                        text: signalRow.angle.toFixed(2)
                        invalidInput: false
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                matrix.controller.selectSignal(matrix.groupIndex, signalRow.rowIndex)
                                text = signalRow.angle.toFixed(2)
                                selectAll()
                            }
                        }
                        onTextEdited: {
                            var value = matrix.controller.parseOperatorNumber(text)
                            if (matrix.controller.validPhase(value)) {
                                invalidInput = false
                                signalRow.pendingPhase = value
                                phaseApplyTimer.restart()
                            } else {
                                invalidInput = true
                            }
                        }
                        onEditingFinished: {
                            var value = matrix.controller.parseOperatorNumber(text)
                            if (!matrix.controller.validPhase(value)) {
                                phaseApplyTimer.stop()
                                text = signalRow.angle.toFixed(2)
                                invalidInput = false
                                matrix.controller.showMessage(signalRow.sid + " phase is outside the supported range.", true)
                            } else {
                                signalRow.pendingPhase = value
                                phaseApplyTimer.stop()
                                matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "phase", value)
                                text = value.toFixed(2)
                            }
                        }
                        Keys.onPressed: function(event) {
                            if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0) {
                                matrix.controller.navigate(matrix.groupIndex, signalRow.rowIndex, 1, event.key)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                matrix.controller.focusCell(matrix.groupIndex, signalRow.rowIndex + 1, 1)
                                event.accepted = true
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: matrix.theme.lineSoft
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
