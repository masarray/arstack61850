// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var controller
    property string uiFont: "Inter"
    property string monoFont: "Inter"

    function applyShape() {
        controller.ctDcOffsetPercent = Number(dcField.text)
        controller.ctHarmonicPercent = Number(harmonicField.text)
        controller.ctHarmonicOrder = Number(orderField.text)
        controller.ctClipPercent = Number(clipField.text)
        controller.setCtSaturation(enabledCheck.checked)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label { text: "Waveform stress"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold }
        Label { Layout.fillWidth: true; text: "CT saturation uses the ARSVIN-compatible publisher approximation: DC offset, harmonic content, and current clipping. It is a protection-test stress waveform, not a calibrated electromagnetic CT model."; wrapMode: Text.WordWrap; color: panel.theme.textSoft; font.family: panel.uiFont; font.pixelSize: 10; lineHeight: 1.35 }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 62
            radius: 8
            color: panel.controller.ctSaturationEnabled ? panel.theme.amberSoft : panel.theme.surface2
            border.width: 1
            border.color: panel.controller.ctSaturationEnabled ? "#705827" : panel.theme.lineSoft
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                CheckBox { id: enabledCheck; checked: panel.controller.ctSaturationEnabled; text: "Enable CT saturation stress"; font.family: panel.uiFont; font.pixelSize: 10 }
                Item { Layout.fillWidth: true }
                Label { text: panel.controller.ctSaturationEnabled ? "ACTIVE" : "CLEAN SINE"; color: panel.controller.ctSaturationEnabled ? panel.theme.amber : panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8; font.weight: Font.Bold }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 12
            rowSpacing: 10
            Label { text: "Current DC offset"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: dcField; theme: panel.theme; monoFont: panel.uiFont; text: panel.controller.ctDcOffsetPercent.toFixed(1); validator: DoubleValidator { bottom: -300; top: 300; decimals: 1 } }
            Label { text: "% of peak"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Item { Layout.fillWidth: true }

            Label { text: "Harmonic"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: harmonicField; theme: panel.theme; monoFont: panel.uiFont; text: panel.controller.ctHarmonicPercent.toFixed(1); validator: DoubleValidator { bottom: 0; top: 300; decimals: 1 } }
            Label { text: "%"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            NumericField { id: orderField; theme: panel.theme; monoFont: panel.uiFont; text: String(panel.controller.ctHarmonicOrder); validator: IntValidator { bottom: 2; top: 63 } }

            Label { text: "Current clip"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: clipField; theme: panel.theme; monoFont: panel.uiFont; text: panel.controller.ctClipPercent.toFixed(1); validator: DoubleValidator { bottom: 1; top: 1000; decimals: 1 } }
            Label { text: "% of peak"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "ARSVIN CT preset"; onClicked: { dcField.text = "30.0"; harmonicField.text = "28.0"; orderField.text = "2"; clipField.text = "60.0"; enabledCheck.checked = true; panel.applyShape() } }
            Item { Layout.fillWidth: true }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; tone: "accent"; text: "Apply waveform"; enabled: panel.controller.signalFrequency > 0; onClicked: panel.applyShape() }
        }

        Item { Layout.fillHeight: true }
    }
}
