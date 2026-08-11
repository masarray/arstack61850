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
    radius: 8
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
            Layout.preferredHeight: 40
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 8

            Rectangle {
                width: 20
                height: 20
                radius: 5
                color: "#122033"
                border.width: 1
                border.color: "#223955"
                Label {
                    anchors.centerIn: parent
                    text: matrix.symbolText
                    color: matrix.theme.accent
                    font.family: matrix.monoFont
                    font.pixelSize: 9
                    font.weight: Font.Bold
                }
            }
            Label {
                text: matrix.titleText
                color: matrix.theme.text
                font.family: matrix.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            Label {
                text: matrix.unitText
                color: matrix.theme.muted
                font.family: matrix.uiFont
                font.pixelSize: 9
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: matrix.theme.lineSoft }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            Layout.leftMargin: 9
            Layout.rightMargin: 9
            spacing: 7
            Label { text: "ON"; Layout.preferredWidth: 28; Layout.alignment: Qt.AlignVCenter; color: matrix.theme.muted; font.family: matrix.uiFont; font.pixelSize: matrix.theme.captionSize - 1; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
            Label { text: "CH"; Layout.preferredWidth: 34; Layout.alignment: Qt.AlignVCenter; color: matrix.theme.muted; font.family: matrix.uiFont; font.pixelSize: matrix.theme.captionSize - 1; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
            Label { text: "MAGNITUDE"; Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; color: matrix.theme.muted; font.family: matrix.uiFont; font.pixelSize: matrix.theme.captionSize - 1; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
            Label { text: "PHASE"; Layout.preferredWidth: matrix.compact ? 82 : 98; Layout.alignment: Qt.AlignVCenter; color: matrix.theme.muted; font.family: matrix.uiFont; font.pixelSize: matrix.theme.captionSize - 1; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
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

                Layout.fillWidth: true
                Layout.preferredHeight: matrix.compact ? 50 : 54
                color: signalRow.selected ? "#111c27"
                    : (magnitudeField.activeFocus || phaseField.activeFocus) ? "#131f2b"
                    : rowHover.hovered ? "#101820" : "transparent"
                Behavior on color { ColorAnimation { duration: 90 } }

                HoverHandler { id: rowHover }

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
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 7

                    CheckBox {
                        Layout.preferredWidth: 28
                        Layout.alignment: Qt.AlignVCenter
                        checked: signalRow.channelEnabled
                        onToggled: {
                            matrix.sourceModel.setProperty(signalRow.rowIndex, "enabled", checked)
                            matrix.controller.selectSignal(matrix.groupIndex, signalRow.rowIndex)
                            matrix.controller.refreshPreview()
                            if (matrix.device.deviceVerified)
                                matrix.device.setEnabled(signalRow.sid, checked)
                        }
                    }

                    RowLayout {
                        Layout.preferredWidth: 34
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 5
                        Rectangle {
                            width: 7
                            height: 7
                            radius: 3.5
                            color: signalRow.phaseColor
                        }
                        Label {
                            text: signalRow.sid
                            color: signalRow.selected ? matrix.theme.text : matrix.theme.textSoft
                            font.family: matrix.monoFont
                            font.pixelSize: 9
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
                                matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "magnitude", value)
                            } else {
                                invalidInput = true
                            }
                        }
                        onEditingFinished: {
                            var value = matrix.controller.parseOperatorNumber(text)
                            if (!matrix.controller.validMagnitude(matrix.groupIndex, value)) {
                                text = signalRow.mag.toFixed(3)
                                invalidInput = false
                                matrix.controller.showMessage(signalRow.sid + " magnitude is outside the valid wire/scaling range.", true)
                            } else {
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
                        Layout.preferredWidth: matrix.compact ? 82 : 98
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
                                matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "phase", value)
                            } else {
                                invalidInput = true
                            }
                        }
                        onEditingFinished: {
                            var value = matrix.controller.parseOperatorNumber(text)
                            if (!matrix.controller.validPhase(value)) {
                                text = signalRow.angle.toFixed(2)
                                invalidInput = false
                                matrix.controller.showMessage(signalRow.sid + " phase must stay within ±360000°.", true)
                            } else {
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
