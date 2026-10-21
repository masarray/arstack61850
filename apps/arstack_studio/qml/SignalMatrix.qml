// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: matrix
    property var theme
    property var controller
    property var device
    property var session
    property var sourceModel
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property int groupIndex: 0
    property string titleText: "Current"
    property string symbolText: "I"
    property string unitText: "A RMS"
    property bool compact: false

    color: matrix.theme.surface2
    radius: 7
    border.width: 1
    border.color: matrix.theme.lineSoft

    function phaseColorFor(index) {
        return [theme.phaseA, theme.phaseB, theme.phaseC, theme.phaseN][index]
    }

    // START flushes the same editor-commit path as Enter/Tab/blur.
    // A complete value is sent once, never during partial keyboard input.
    function flushPendingEdits() {
        for (var i = 0; i < rowRepeater.count; ++i) {
            const row = rowRepeater.itemAt(i)
            if (row && !row.flushPendingEdits()) return false
        }
        return true
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
            Layout.preferredHeight: 32
            Layout.leftMargin: 11
            Layout.rightMargin: 11
            spacing: 7
            Label {
                text: matrix.titleText
                color: matrix.theme.text
                font.family: matrix.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            Label {
                text: matrix.unitText
                color: matrix.theme.muted
                font.family: matrix.uiFont
                font.pixelSize: 8
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: matrix.theme.lineSoft }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Layout.leftMargin: 9
            Layout.rightMargin: 9
            spacing: 6
            Label {
                text: "CH"
                Layout.preferredWidth: 46
                color: matrix.theme.muted2
                font.family: matrix.monoFont
                font.pixelSize: 7
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Label {
                text: "MAGNITUDE"
                Layout.fillWidth: true
                color: matrix.theme.muted2
                font.family: matrix.uiFont
                font.pixelSize: 7
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            Label {
                text: "ANGLE"
                Layout.preferredWidth: matrix.compact ? 84 : 100
                color: matrix.theme.muted2
                font.family: matrix.uiFont
                font.pixelSize: 7
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
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
                property real signalQuality: model.quality
                property color phaseColor: matrix.phaseColorFor(rowIndex)
                property bool selected: matrix.controller.activeSignal === signalRow.sid
                property alias magnitudeEditor: magnitudeField
                property alias phaseEditor: phaseField
                // Editing draft belongs to the field until commit. Never
                // queue SET commands with a 90ms debounce while typing.
                property bool magnitudeDirty: false
                property bool phaseDirty: false
                function commitMagnitude() {
                    if (!magnitudeDirty) return true
                    const value = matrix.controller.parseOperatorNumber(magnitudeField.text)
                    magnitudeDirty = false
                    if (!matrix.controller.validMagnitude(matrix.groupIndex, value)) {
                        magnitudeField.text = mag.toFixed(3)
                        magnitudeField.invalidInput = false
                        matrix.controller.showMessage(
                            sid + ": invalid magnitude. Previous value restored. Use 0.5 or 0,5.", true)
                        return false
                    }
                    magnitudeField.invalidInput = false
                    magnitudeField.text = value.toFixed(3)
                    if (value === mag) return true
                    return matrix.controller.editSignal(matrix.groupIndex, rowIndex, "magnitude", value)
                }
                function commitPhase() {
                    if (!phaseDirty) return true
                    const value = matrix.controller.parseOperatorNumber(phaseField.text)
                    phaseDirty = false
                    if (!matrix.controller.validPhase(value)) {
                        phaseField.text = angle.toFixed(2)
                        phaseField.invalidInput = false
                        matrix.controller.showMessage(
                            sid + ": invalid phase. Previous angle restored. Use 0.5 or 0,5.", true)
                        return false
                    }
                    phaseField.invalidInput = false
                    phaseField.text = value.toFixed(2)
                    if (value === angle) return true
                    return matrix.controller.editSignal(matrix.groupIndex, rowIndex, "phase", value)
                }
                function flushPendingEdits() {
                    return commitMagnitude() && commitPhase()
                }

                // Align with the field edge, not the left side of the table.
                // Clamp inside the owning matrix and open below the editor;
                // keyboard Menu/Shift+F10 uses exactly the same placement.
                function openFieldMenu(field, actionMenu) {
                    const edge = signalRow.mapFromItem(field, field.width, field.height)
                    const x = Math.max(6, Math.min(edge.x - actionMenu.width,
                                                   signalRow.width - actionMenu.width - 6))
                    actionMenu.popup(x, edge.y + 3)
                }

                // Action-backed menu uses a single globally styled delegate.
                // Rows/callbacks stay local so keyboard navigation and model
                // ownership remain unchanged.
                StudioActionMenu {
                    id: magnitudeMenu
                    theme: matrix.theme
                    uiFont: matrix.uiFont
                    contextLabel: "Magnitude · " + signalRow.sid
                    commandRows: 4
                    Action {
                        text: "Zero · 0"
                        icon.source: Qt.resolvedUrl("../assets/material/block.svg")
                        onTriggered: matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "magnitude", 0)
                    }
                    Action {
                        text: "50% nominal (demo)"
                        icon.source: Qt.resolvedUrl("../assets/material/tune.svg")
                        onTriggered: matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex,
                            "magnitude", matrix.groupIndex === 0 ? 0.5 : 28.87)
                    }
                    Action {
                        text: "100% nominal (demo)"
                        icon.source: Qt.resolvedUrl("../assets/material/check_circle.svg")
                        onTriggered: matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex,
                            "magnitude", matrix.groupIndex === 0 ? 1.0 : 57.74)
                    }
                    StudioMenuDivider { theme: matrix.theme }
                    Action {
                        text: "Equal magnitudes A/B/C"
                        icon.source: Qt.resolvedUrl("../assets/material/done_all.svg")
                        enabled: signalRow.rowIndex < 3
                        onTriggered: matrix.controller.equalGroupMagnitudes(matrix.groupIndex, signalRow.rowIndex)
                    }
                }
                StudioActionMenu {
                    id: phaseMenu
                    theme: matrix.theme
                    uiFont: matrix.uiFont
                    contextLabel: "Angle · " + signalRow.sid
                    commandRows: 3
                    Action {
                        text: "Zero angle · 0°"
                        icon.source: Qt.resolvedUrl("../assets/material/block.svg")
                        enabled: matrix.controller.signalFrequency > 0
                        onTriggered: matrix.controller.editSignal(matrix.groupIndex, signalRow.rowIndex, "phase", 0)
                    }
                    StudioMenuDivider { theme: matrix.theme }
                    Action {
                        text: "Equal angles A/B/C"
                        icon.source: Qt.resolvedUrl("../assets/material/done_all.svg")
                        enabled: signalRow.rowIndex < 3
                        onTriggered: matrix.controller.equalGroupAngles(matrix.groupIndex, signalRow.rowIndex)
                    }
                    Action {
                        text: "Balance angles · 120°"
                        icon.source: Qt.resolvedUrl("../assets/material/balance.svg")
                        enabled: signalRow.rowIndex < 3
                        onTriggered: matrix.controller.balanceGroupAngles(matrix.groupIndex, signalRow.rowIndex)
                    }
                }

                Layout.fillWidth: true
                // Distribute spare workspace across editable channel rows;
                // never leave a large dead zone under four fixed-height rows.
                Layout.fillHeight: true
                Layout.minimumHeight: 34
                Layout.preferredHeight: matrix.compact ? 40 : 42
                color: signalRow.selected ? "#101c27"
                    : (magnitudeField.activeFocus || phaseField.activeFocus) ? "#0f1922"
                    : rowHover.hovered ? "#0d161e" : "transparent"
                Behavior on color { ColorAnimation { duration: 90 } }

                HoverHandler { id: rowHover }

                // No per-keystroke timers. Enter, blur, Tab and START each
                // invoke the same idempotent editor commit functions.
                onMagChanged: if (!magnitudeField.activeFocus) magnitudeField.text = mag.toFixed(3)
                onAngleChanged: if (!phaseField.activeFocus) phaseField.text = angle.toFixed(2)

                Rectangle {
                    visible: signalRow.selected
                    width: 2
                    opacity: 0.86
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: signalRow.phaseColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 6

                    RowLayout {
                        Layout.preferredWidth: 46
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 6
                        Rectangle { width: 6; height: 6; radius: 3; color: signalRow.phaseColor }
                        Label {
                            text: signalRow.sid
                            color: signalRow.selected ? matrix.theme.text : matrix.theme.textSoft
                            font.family: matrix.monoFont
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    NumericField {
                        id: magnitudeField
                        objectName: "matrixMagnitudeEditor"
                        ToolTip.visible: hovered && !activeFocus && !magnitudeMenu.visible && !phaseMenu.visible
                        ToolTip.delay: 650
                        ToolTip.text: "Right-click · Shift+F10"
                        MouseArea {
                            anchors.fill: parent
                            z: 5
                            acceptedButtons: Qt.RightButton
                            onClicked: signalRow.openFieldMenu(magnitudeField, magnitudeMenu)
                        }
                        Layout.fillWidth: true
                        theme: matrix.theme
                        monoFont: matrix.monoFont
                        compact: true
                        suffixText: matrix.groupIndex === 0 ? "A" : "V"
                        text: signalRow.mag.toFixed(3)
                        invalidInput: false
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                matrix.controller.selectSignal(matrix.groupIndex, signalRow.rowIndex)
                                text = signalRow.mag.toFixed(3)
                                signalRow.magnitudeDirty = false
                                selectAll()
                            } else signalRow.commitMagnitude()
                        }
                        onTextEdited: {
                            signalRow.magnitudeDirty = true
                            const value = matrix.controller.parseOperatorNumber(text)
                            invalidInput = !matrix.controller.validMagnitude(matrix.groupIndex, value)
                        }
                        onEditingFinished: signalRow.commitMagnitude()
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Menu ||
                                    (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                signalRow.openFieldMenu(magnitudeField, magnitudeMenu)
                                event.accepted = true
                            } else if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0) {
                                matrix.controller.navigate(matrix.groupIndex, signalRow.rowIndex, 0, event.key)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                if (signalRow.commitMagnitude())
                                    matrix.controller.focusCell(matrix.groupIndex, signalRow.rowIndex + 1, 0)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape) {
                                signalRow.magnitudeDirty = false
                                magnitudeField.invalidInput = false
                                magnitudeField.text = signalRow.mag.toFixed(3)
                                event.accepted = true
                            }
                        }
                    }

                    NumericField {
                        id: phaseField
                        objectName: "matrixPhaseEditor"
                        ToolTip.visible: hovered && !activeFocus && !magnitudeMenu.visible && !phaseMenu.visible
                        ToolTip.delay: 650
                        ToolTip.text: "Right-click · Shift+F10"
                        MouseArea {
                            anchors.fill: parent
                            z: 5
                            acceptedButtons: Qt.RightButton
                            onClicked: signalRow.openFieldMenu(phaseField, phaseMenu)
                        }
                        enabled: matrix.controller.signalFrequency > 0
                        Layout.preferredWidth: matrix.compact ? 84 : 100
                        theme: matrix.theme
                        monoFont: matrix.monoFont
                        compact: true
                        suffixText: "°"
                        text: signalRow.angle.toFixed(2)
                        invalidInput: false
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                matrix.controller.selectSignal(matrix.groupIndex, signalRow.rowIndex)
                                text = signalRow.angle.toFixed(2)
                                signalRow.phaseDirty = false
                                selectAll()
                            } else signalRow.commitPhase()
                        }
                        onTextEdited: {
                            signalRow.phaseDirty = true
                            const value = matrix.controller.parseOperatorNumber(text)
                            invalidInput = !matrix.controller.validPhase(value)
                        }
                        onEditingFinished: signalRow.commitPhase()
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Menu ||
                                    (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                signalRow.openFieldMenu(phaseField, phaseMenu)
                                event.accepted = true
                            } else if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0) {
                                matrix.controller.navigate(matrix.groupIndex, signalRow.rowIndex, 1, event.key)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                if (signalRow.commitPhase())
                                    matrix.controller.focusCell(matrix.groupIndex, signalRow.rowIndex + 1, 1)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape) {
                                signalRow.phaseDirty = false
                                phaseField.invalidInput = false
                                phaseField.text = signalRow.angle.toFixed(2)
                                event.accepted = true
                            }
                        }
                    }
                }

                Rectangle {
                    visible: signalRow.rowIndex < 3
                    anchors.left: parent.left
                    anchors.leftMargin: 9
                    anchors.right: parent.right
                    anchors.rightMargin: 9
                    anchors.bottom: parent.bottom
                    height: 1
                    color: matrix.theme.lineSoft
                }
            }
        }

        // Channel delegates stretch into available height.
    }
}
