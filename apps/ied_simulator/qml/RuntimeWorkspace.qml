// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var backend
    signal addIedRequested()
    signal folderRequested()

    property string pendingValue: backend.selectedValue.value || ""
    property string pendingQuality: backend.selectedValue.quality || "Good"
    property string pendingOrigin: backend.selectedValue.origin || "Simulator"
    property bool activityExpanded: true

    function severityColor(severity) {
        if (severity === "Error") return theme.red
        if (severity === "Warning") return theme.amber
        if (severity === "Success") return theme.green
        return theme.accent
    }

    function refreshEditor() {
        pendingValue = backend.selectedValue.value || ""
        pendingQuality = backend.selectedValue.quality || "Good"
        pendingOrigin = backend.selectedValue.origin || "Simulator"
    }

    Connections {
        target: backend
        function onSelectionChanged() { root.refreshEditor() }
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10
                Rectangle {
                    width: 30; height: 30; radius: 7
                    color: theme.accentSoft; border.width: 1; border.color: theme.accent
                    Image { anchors.centerIn: parent; width: 17; height: 17; source: "qrc:/iedsim/assets/radio-tower.svg" }
                }
                Label {
                    text: "ARSTACK61850  /  IED Simulator"
                    color: theme.text
                    font.pixelSize: theme.subtitleSize
                    font.weight: Font.Medium
                    Layout.alignment: Qt.AlignVCenter
                }
                Rectangle { width: 1; height: 26; color: theme.line; Layout.leftMargin: 6; Layout.rightMargin: 6 }
                ActionButton { theme: root.theme; text: "Add IED"; iconSource: "qrc:/iedsim/assets/upload.svg"; onClicked: root.addIedRequested() }
                Item { Layout.fillWidth: true }
                Label {
                    text: backend.ieds.length + " IED" + (backend.ieds.length === 1 ? "" : "s") + " · " + backend.listenAddress + ":" + backend.port
                    color: theme.textSoft; font.pixelSize: theme.captionSize; font.features: { "tnum": 1 }
                }
                StatusPill { theme: root.theme; text: "Running"; tone: theme.green; fill: theme.greenSoft }
                ActionButton {
                    theme: root.theme; text: "Stop"; danger: true
                    iconSource: "qrc:/iedsim/assets/square.svg"
                    onClicked: backend.stopSimulation()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            Layout.bottomMargin: 8
            spacing: 10

            SurfaceCard {
                theme: root.theme
                Layout.fillHeight: true
                Layout.preferredWidth: 252
                Layout.minimumWidth: 220
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "IED Fleet"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        StatusPill { theme: root.theme; text: backend.ieds.length.toString(); tone: theme.accent; fill: theme.accentSoft }
                    }
                    TextField {
                        Layout.fillWidth: true; implicitHeight: 34
                        placeholderText: "Search IEDs"
                        color: theme.text; placeholderTextColor: theme.muted; font.pixelSize: theme.captionSize
                        leftPadding: 10
                        background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 7; model: backend.ieds
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width; height: 84; radius: 7
                            color: index === backend.selectedIedIndex ? theme.accentSoft : theme.surfaceRaised
                            border.width: 1
                            border.color: index === backend.selectedIedIndex ? theme.accent : theme.lineSoft
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 10; spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Rectangle { width: 8; height: 8; radius: 4; color: modelData.status === "Running" ? theme.green : theme.amber }
                                    Label { text: modelData.name || "Unnamed IED"; color: theme.text; font.pixelSize: theme.labelSize; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: modelData.status; color: modelData.status === "Running" ? theme.green : theme.amber; font.pixelSize: 10 }
                                }
                                Label { text: modelData.manufacturer || "Engineering model"; color: theme.muted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: modelData.endpoint; color: theme.textSoft; font.pixelSize: 10; font.features: { "tnum": 1 } }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.selectedIedIndex = index }
                        }
                    }
                    ActionButton { theme: root.theme; Layout.fillWidth: true; text: "Add another IED"; iconSource: "qrc:/iedsim/assets/upload.svg"; onClicked: root.addIedRequested() }
                }
            }

            SurfaceCard {
                theme: root.theme
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2.2
                ColumnLayout {
                    anchors.fill: parent; spacing: 0

                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 58
                        Layout.leftMargin: 16; Layout.rightMargin: 16; spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Label { text: "Model Explorer"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold }
                            Label {
                                text: (backend.selectedIed.name || "IED") + "  /  " + (backend.selectedValue.logicalDevice || "Model") + "  /  " + (backend.selectedValue.logicalNode || "Values")
                                color: theme.muted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle
                            }
                        }
                        Label { text: backend.values.length + " attributes"; color: theme.muted; font.pixelSize: theme.captionSize }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 48
                        Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 8
                        TextField {
                            Layout.fillWidth: true; implicitHeight: 34
                            placeholderText: "Search by object, reference, FC, or value"
                            color: theme.text; placeholderTextColor: theme.muted; font.pixelSize: theme.captionSize
                            background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                        }
                        ComboBox {
                            implicitWidth: 118; implicitHeight: 34
                            model: ["All FC", "ST", "MX", "CF", "CO", "OR"]
                            contentItem: Label { leftPadding: 9; text: parent.displayText; color: theme.textSoft; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                            background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: theme.line }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        color: theme.chrome
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                            Label { text: "OBJECT"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; Layout.preferredWidth: 220; Layout.minimumWidth: 220; Layout.maximumWidth: 220 }
                            Label { text: "FC"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; Layout.preferredWidth: 42; Layout.minimumWidth: 42; Layout.maximumWidth: 42 }
                            Label { text: "TYPE"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; Layout.preferredWidth: 94; Layout.minimumWidth: 94; Layout.maximumWidth: 94 }
                            Label { text: "CURRENT VALUE"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; Layout.fillWidth: true }
                            Label { text: "QUALITY"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; Layout.preferredWidth: 72 }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; model: backend.values
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width; height: 46
                            color: index === backend.selectedValueIndex ? theme.accentSoft : (index % 2 ? theme.surface : theme.surfaceRaised)
                            border.width: index === backend.selectedValueIndex ? 1 : 0
                            border.color: theme.accent
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                                ColumnLayout {
                                    Layout.preferredWidth: 220; Layout.minimumWidth: 220; Layout.maximumWidth: 220; spacing: 1
                                    Label { text: modelData.name || modelData.dataObject; color: theme.text; font.pixelSize: theme.captionSize; font.weight: Font.Medium; width: 220; elide: Text.ElideRight }
                                    Label { text: modelData.logicalNode + " · " + modelData.dataAttribute; color: theme.muted; font.pixelSize: 9; width: 220; elide: Text.ElideRight }
                                }
                                Label { text: modelData.fc || "—"; color: theme.textSoft; font.pixelSize: 10; Layout.preferredWidth: 42; Layout.minimumWidth: 42; Layout.maximumWidth: 42 }
                                Label { text: modelData.type; color: theme.muted; font.pixelSize: 10; Layout.preferredWidth: 94; Layout.minimumWidth: 94; Layout.maximumWidth: 94; elide: Text.ElideRight }
                                Label { text: modelData.value; color: modelData.changed ? theme.accent : theme.text; font.pixelSize: theme.captionSize; font.weight: modelData.changed ? Font.DemiBold : Font.Normal; Layout.fillWidth: true; elide: Text.ElideRight }
                                RowLayout {
                                    Layout.preferredWidth: 72; spacing: 5
                                    Rectangle { width: 7; height: 7; radius: 4; color: modelData.quality === "Good" ? theme.green : theme.amber }
                                    Label { text: modelData.quality; color: modelData.quality === "Good" ? theme.green : theme.amber; font.pixelSize: 9; elide: Text.ElideRight }
                                }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.selectedValueIndex = index }
                        }
                        Label {
                            anchors.centerIn: parent; visible: backend.values.length === 0
                            text: "No DataSet, report, or GOOSE members were resolved for this IED."
                            color: theme.muted; font.pixelSize: theme.captionSize
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 32
                        color: theme.chrome
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                            Label { text: backend.values.length + " resolved attributes"; color: theme.muted; font.pixelSize: 9; Layout.fillWidth: true }
                            Label { text: "Select a row to edit"; color: theme.muted; font.pixelSize: 9 }
                        }
                    }
                }
            }

            SurfaceCard {
                theme: root.theme
                Layout.fillHeight: true
                Layout.preferredWidth: 350
                Layout.minimumWidth: 320
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 11
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Label { text: "Smart Value Editor"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold }
                            Label { text: "Type-aware, validated, and reversible"; color: theme.muted; font.pixelSize: 10 }
                        }
                        StatusPill {
                            theme: root.theme
                            text: backend.selectedValue.writable ? "Writable" : "Read only"
                            tone: backend.selectedValue.writable ? theme.green : theme.muted
                            fill: backend.selectedValue.writable ? theme.greenSoft : theme.surfaceRaised
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                    Label {
                        Layout.fillWidth: true
                        text: backend.selectedValue.reference || "Select a model value"
                        color: theme.text; font.pixelSize: theme.labelSize; font.weight: Font.DemiBold
                        wrapMode: Text.WrapAnywhere
                    }
                    GridLayout {
                        Layout.fillWidth: true; columns: 2; columnSpacing: 12; rowSpacing: 5
                        Label { text: "Type"; color: theme.muted; font.pixelSize: 10 }
                        Label { text: backend.selectedValue.type || "—"; color: theme.textSoft; font.pixelSize: 10; Layout.fillWidth: true }
                        Label { text: "Functional constraint"; color: theme.muted; font.pixelSize: 10 }
                        Label { text: backend.selectedValue.fc || "—"; color: theme.textSoft; font.pixelSize: 10; Layout.fillWidth: true }
                        Label { text: "Current"; color: theme.muted; font.pixelSize: 10 }
                        Label { text: backend.selectedValue.value || "—"; color: theme.accent; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    Label { text: "CHOOSE NEW VALUE"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    ComboBox {
                        id: optionEditor
                        Layout.fillWidth: true; implicitHeight: 40
                        visible: (backend.selectedValue.options || []).length > 0
                        model: backend.selectedValue.options || []
                        currentIndex: Math.max(0, model.indexOf(root.pendingValue))
                        onActivated: root.pendingValue = currentText
                        contentItem: Label { leftPadding: 10; rightPadding: 28; text: optionEditor.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: theme.labelSize }
                        background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: optionEditor.activeFocus ? theme.accent : theme.line }
                    }
                    TextField {
                        Layout.fillWidth: true; implicitHeight: 40
                        visible: (backend.selectedValue.options || []).length === 0
                        text: root.pendingValue
                        onTextEdited: root.pendingValue = text
                        readOnly: !backend.selectedValue.writable
                        color: theme.text; font.pixelSize: theme.labelSize
                        placeholderText: "Enter a value"; placeholderTextColor: theme.muted
                        background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                    }
                    Label { text: "OPTIONAL ATTRIBUTES"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Quality"; color: theme.textSoft; font.pixelSize: theme.captionSize; Layout.preferredWidth: 74 }
                        ComboBox {
                            id: qualityBox
                            Layout.fillWidth: true; implicitHeight: 34
                            model: ["Good", "Invalid", "Questionable", "Reserved"]
                            currentIndex: Math.max(0, model.indexOf(root.pendingQuality))
                            onActivated: root.pendingQuality = currentText
                            contentItem: Label { leftPadding: 9; rightPadding: 26; text: qualityBox.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                            background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: theme.line }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Origin"; color: theme.textSoft; font.pixelSize: theme.captionSize; Layout.preferredWidth: 74 }
                        ComboBox {
                            id: originBox
                            Layout.fillWidth: true; implicitHeight: 34
                            model: ["Simulator", "Process", "Operator", "Test"]
                            currentIndex: Math.max(0, model.indexOf(root.pendingOrigin))
                            onActivated: root.pendingOrigin = currentText
                            contentItem: Label { leftPadding: 9; rightPadding: 26; text: originBox.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                            background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: theme.line }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 118; radius: 7
                        color: backend.selectedValue.writable ? theme.greenSoft : theme.surfaceRaised
                        border.width: 1
                        border.color: backend.selectedValue.writable ? Qt.rgba(theme.green.r, theme.green.g, theme.green.b, 0.48) : theme.line
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 11; spacing: 5
                            RowLayout {
                                Image { width: 16; height: 16; source: backend.selectedValue.writable ? "qrc:/iedsim/assets/circle-check.svg" : "qrc:/iedsim/assets/settings-2.svg" }
                                Label { text: backend.selectedValue.writable ? "Change is valid" : "Read-only attribute"; color: backend.selectedValue.writable ? theme.green : theme.textSoft; font.pixelSize: theme.captionSize; font.weight: Font.DemiBold }
                            }
                            Label { text: "✓ Value matches the resolved SCL type"; color: theme.textSoft; font.pixelSize: 9 }
                            Label { text: "✓ Functional constraint: " + (backend.selectedValue.fc || "not specified"); color: theme.textSoft; font.pixelSize: 9 }
                            Label { text: "✓ DataSet/report consumers refresh after apply"; color: theme.textSoft; font.pixelSize: 9 }
                            Label { text: "✓ Previous value can be restored with Undo"; color: theme.textSoft; font.pixelSize: 9 }
                        }
                    }
                    Item { Layout.preferredHeight: 4 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        ActionButton { theme: root.theme; Layout.fillWidth: true; text: "Undo"; enabled: backend.running; onClicked: backend.undoLastChange() }
                        ActionButton {
                            theme: root.theme; Layout.fillWidth: true; text: "Apply Value"; primary: true
                            enabled: backend.running && backend.selectedValue.writable && root.pendingValue.length > 0
                            onClicked: backend.applySelectedValue(root.pendingValue, root.pendingQuality, root.pendingOrigin)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "All changes are recorded in the activity log."
                        color: theme.muted; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        SurfaceCard {
            theme: root.theme
            Layout.fillWidth: true
            Layout.preferredHeight: root.activityExpanded ? 176 : 42
            Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 12
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 42; color: "transparent"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                        Image { width: 15; height: 15; source: "qrc:/iedsim/assets/activity.svg" }
                        Label { text: "Live Activity"; color: theme.textSoft; font.pixelSize: theme.labelSize; font.weight: Font.DemiBold }
                        StatusPill { theme: root.theme; text: "MMS online"; tone: theme.green; fill: theme.greenSoft }
                        Item { Layout.fillWidth: true }
                        Label { text: backend.activity.length + " events"; color: theme.muted; font.pixelSize: 10 }
                        ActionButton { theme: root.theme; text: "Copy diagnostics"; implicitHeight: 28; onClicked: backend.copyDiagnostics() }
                        ActionButton { theme: root.theme; text: "Clear"; implicitHeight: 28; onClicked: backend.clearActivity() }
                        Image { width: 15; height: 15; source: root.activityExpanded ? "qrc:/iedsim/assets/chevron-down.svg" : "qrc:/iedsim/assets/chevron-up.svg" }
                    }
                    TapHandler { onTapped: root.activityExpanded = !root.activityExpanded }
                }
                Rectangle { visible: root.activityExpanded; Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                ListView {
                    visible: root.activityExpanded
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: backend.activity
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width; height: 31; spacing: 10
                        Item { width: 10 }
                        Label { text: modelData.time; color: theme.muted; font.pixelSize: 9; Layout.preferredWidth: 82 }
                        Rectangle { width: 7; height: 7; radius: 4; color: root.severityColor(modelData.severity) }
                        Label { text: modelData.category; color: theme.textSoft; font.pixelSize: 9; font.weight: Font.DemiBold; Layout.preferredWidth: 72 }
                        Label { text: modelData.message; color: theme.textSoft; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: modelData.severity; color: root.severityColor(modelData.severity); font.pixelSize: 9; Layout.preferredWidth: 58 }
                        Item { width: 8 }
                    }
                }
            }
        }
    }
}
