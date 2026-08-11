// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

SurfacePanel {
    id: panel
    property var controller
    property var device
    property var profiles
    property string uiFont: "Inter"
    property string monoFont: "Cascadia Mono"
    property bool compact: false

    readonly property var profile: profiles.selectedProfile
    readonly property var issueItems: {
        var out = []
        if (profiles.fatalError && profiles.fatalError.length)
            out.push({ severity: "error", text: profiles.fatalError })
        var errors = profile.errors || []
        for (var i = 0; i < errors.length && out.length < 2; ++i)
            out.push({ severity: "error", text: errors[i] })
        var warnings = profile.warnings || []
        for (var j = 0; j < warnings.length && out.length < 2; ++j)
            out.push({ severity: "warning", text: warnings[j] })
        var documentWarnings = profiles.documentWarnings || []
        for (var k = 0; k < documentWarnings.length && out.length < 2; ++k)
            out.push({ severity: "warning", text: documentWarnings[k] })
        return out
    }

    function kicker(textValue) {
        return textValue
    }

    FileDialog {
        id: engineeringFileDialog
        title: "Open IEC 61850 engineering file"
        nameFilters: [
            "IEC 61850 SCL (*.scd *.cid *.icd *.iid *.ssd *.xml)",
            "All files (*)"
        ]
        onAccepted: {
            if (panel.profiles.loadFile(selectedFile)) {
                panel.controller.profileDirty = true
                panel.controller.showMessage(panel.profiles.documentStatus, false)
            } else {
                panel.controller.showMessage(panel.profiles.fatalError || "Unable to load engineering file.", true)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: panel.compact ? 11 : 14
        spacing: panel.compact ? 8 : 10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: "ENGINEERING SOURCE"
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.0
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: panel.profiles.hasProfiles ? (panel.profile.svId || "Compiled SCL") : "Development profile"
                    color: panel.theme.text
                    font.family: panel.uiFont
                    font.pixelSize: panel.compact ? 13 : 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: panel.controller.profileDirty && panel.profiles.hasProfiles
                    implicitHeight: 20
                    implicitWidth: dirtyText.implicitWidth + 14
                    radius: 5
                    color: panel.theme.amberSoft
                    border.width: 1
                    border.color: "#705a31"
                    Label {
                        id: dirtyText
                        anchors.centerIn: parent
                        text: "DEPLOY"
                        color: panel.theme.amber
                        font.family: panel.monoFont
                        font.pixelSize: 7
                        font.weight: Font.Bold
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: panel.profiles.sourceName.length ? panel.profiles.sourceName : "SCL / CID / SCD / IID"
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 9
                elide: Text.ElideMiddle
            }
        }

        CalmButton {
            Layout.fillWidth: true
            theme: panel.theme
            uiFont: panel.uiFont
            text: "Open SCL / CID"
            enabled: !panel.device.running
            onClicked: engineeringFileDialog.open()
        }

        ColumnLayout {
            visible: panel.profiles.hasProfiles
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: "RESOLVED STREAM"
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.0
            }

            ComboBox {
                Layout.fillWidth: true
                model: panel.profiles
                textRole: "control"
                currentIndex: panel.profiles.selectedIndex
                enabled: !panel.device.running
                font.family: panel.uiFont
                font.pixelSize: 9
                onActivated: {
                    panel.profiles.selectStream(currentIndex)
                    panel.controller.profileDirty = true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    implicitHeight: 21
                    implicitWidth: classText.implicitWidth + 14
                    radius: 5
                    color: panel.profile.compatibilityClass === "A" ? panel.theme.greenSoft :
                           panel.profile.compatibilityClass === "B" ? panel.theme.amberSoft : panel.theme.redSoft
                    border.width: 1
                    border.color: panel.profile.compatibilityClass === "A" ? "#2b674d" :
                                  panel.profile.compatibilityClass === "B" ? "#705a31" : "#78404a"
                    Label {
                        id: classText
                        anchors.centerIn: parent
                        text: "CLASS " + (panel.profile.compatibilityClass || "—")
                        color: panel.profile.compatibilityClass === "A" ? panel.theme.green :
                               panel.profile.compatibilityClass === "B" ? panel.theme.amber : panel.theme.red
                        font.family: panel.monoFont
                        font.pixelSize: 7
                        font.weight: Font.Bold
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: String(panel.profile.deviceSupport || "—").toUpperCase()
                    color: panel.profile.deviceSupport === "ready" ? panel.theme.green : panel.theme.textSoft
                    font.family: panel.monoFont
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: identityGrid.implicitHeight + 16
                radius: 6
                color: panel.theme.surface2
                border.width: 1
                border.color: panel.theme.lineSoft

                GridLayout {
                    id: identityGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4

                    Label { text: "svID"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { Layout.fillWidth: true; text: panel.profile.svId || "—"; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; elide: Text.ElideMiddle }
                    Label { text: "DataSet"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { Layout.fillWidth: true; text: panel.profile.dataSetReference || "—"; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; elide: Text.ElideMiddle }
                    Label { text: "MAC / APPID"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { Layout.fillWidth: true; text: (panel.profile.destinationMac || "—") + " · " + (panel.profile.appIdHex || "—"); color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; elide: Text.ElideRight }
                    Label { text: "Rate"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.profile.publisherRate ? panel.profile.publisherRate + " fps" : "—"; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8 }
                    Label { text: "VLAN"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.profile.vlanPresent ? "PCP " + panel.profile.vlanPriority + " · VID " + panel.profile.vlanId : "untagged"; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8 }
                    Label { text: "Payload"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.profile.payloadBytes ? panel.profile.payloadBytes + " B · " + panel.profile.channelLeafCount + " leaves" : "—"; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8 }
                }
            }

            ColumnLayout {
                visible: panel.issueItems.length > 0
                Layout.fillWidth: true
                spacing: 3
                Repeater {
                    model: panel.issueItems
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: (modelData.severity === "error" ? "×  " : "!  ") + modelData.text
                        color: modelData.severity === "error" ? panel.theme.red : panel.theme.amber
                        font.family: panel.uiFont
                        font.pixelSize: 8
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }

        ColumnLayout {
            visible: panel.profile.compatibilityClass === "B"
            Layout.fillWidth: true
            spacing: 5
            Label { text: "COUNTER POLICY"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }
            Label {
                Layout.fillWidth: true
                text: "Confirm the evidenced smpCnt modulus before deployment."
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                NumericField {
                    id: counterField
                    Layout.fillWidth: true
                    theme: panel.theme
                    monoFont: panel.monoFont
                    compact: true
                    text: panel.profile.counterModulus || ""
                    validator: IntValidator { bottom: 1; top: 65535 }
                }
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    text: "Confirm"
                    onClicked: {
                        if (counterField.acceptableInput && panel.profiles.confirmCounterModulus(parseInt(counterField.text))) {
                            panel.controller.profileDirty = true
                            panel.controller.showMessage("Sample-counter modulus confirmed.", false)
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            Label { text: "ENGINEERING SCALING"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "I"; Layout.preferredWidth: 16; color: panel.theme.muted; font.family: panel.monoFont; font.pixelSize: 9 }
                NumericField {
                    Layout.fillWidth: true
                    theme: panel.theme
                    monoFont: panel.monoFont
                    compact: true
                    text: panel.controller.currentScale.toString()
                    validator: DoubleValidator { bottom: 0.000001; top: 2147483647; decimals: 6 }
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    onEditingFinished: {
                        var value = panel.controller.parseOperatorNumber(text)
                        if (acceptableInput && isFinite(value) && value > 0) {
                            panel.controller.currentScale = value
                            if (panel.device.connected) panel.controller.applyGroupSignals(0)
                        } else {
                            text = panel.controller.currentScale.toString()
                        }
                    }
                }
                Label { text: "ct/A"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "U"; Layout.preferredWidth: 16; color: panel.theme.muted; font.family: panel.monoFont; font.pixelSize: 9 }
                NumericField {
                    Layout.fillWidth: true
                    theme: panel.theme
                    monoFont: panel.monoFont
                    compact: true
                    text: panel.controller.voltageScale.toString()
                    validator: DoubleValidator { bottom: 0.000001; top: 2147483647; decimals: 6 }
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    onEditingFinished: {
                        var value = panel.controller.parseOperatorNumber(text)
                        if (acceptableInput && isFinite(value) && value > 0) {
                            panel.controller.voltageScale = value
                            if (panel.device.connected) panel.controller.applyGroupSignals(1)
                        } else {
                            text = panel.controller.voltageScale.toString()
                        }
                    }
                }
                Label { text: "ct/V"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }

        Label { text: "QUICK SETUP"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }
        RowLayout {
            Layout.fillWidth: true
            CalmButton { Layout.fillWidth: true; theme: panel.theme; uiFont: panel.uiFont; text: "Balanced"; onClicked: panel.controller.balanced() }
            CalmButton { Layout.fillWidth: true; theme: panel.theme; uiFont: panel.uiFont; text: "Zero"; onClicked: panel.controller.zeroAll() }
        }

        Item { Layout.fillHeight: true }

        Label { text: "RUNTIME"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 3
            Label { text: "Rate"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.device.fps; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; Layout.alignment: Qt.AlignRight }
            Label { text: "Missed"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.device.missed; color: Number(panel.device.missed) > 0 ? panel.theme.amber : panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; Layout.alignment: Qt.AlignRight }
            Label { text: "TX fail"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.device.txFailures; color: Number(panel.device.txFailures) > 0 ? panel.theme.red : panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; Layout.alignment: Qt.AlignRight }
            Label { text: "Generation"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.device.signalGeneration; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; Layout.alignment: Qt.AlignRight }
        }
    }
}