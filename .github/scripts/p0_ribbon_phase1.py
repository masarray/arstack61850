from pathlib import Path
import re

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f'{label}: expected source block not found')
    return text.replace(old, new, 1)

# Register QML components.
cmake_path = ROOT / 'apps/arstack_studio/CMakeLists.txt'
cmake = cmake_path.read_text(encoding='utf-8')
needle = '        qml/WorkflowBar.qml\n        qml/CalmButton.qml\n'
replacement = ('        qml/WorkflowBar.qml\n'
               '        qml/ModernRibbon.qml\n'
               '        qml/RibbonGroup.qml\n'
               '        qml/RibbonAction.qml\n'
               '        qml/CalmButton.qml\n')
cmake = replace_once(cmake, needle, replacement, 'CMake ribbon registration')
cmake_path.write_text(cmake, encoding='utf-8')

# Theme: define every surface token used by QML. This fixes the white PTP card.
theme_path = ROOT / 'apps/arstack_studio/qml/StudioTheme.qml'
theme = theme_path.read_text(encoding='utf-8')
theme = replace_once(theme,
    '    readonly property color surface2: "#0e141b"\n    readonly property color raised: "#151d27"\n',
    '    readonly property color surface2: "#0e141b"\n    readonly property color panelAlt: "#101820"\n    readonly property color raised: "#151d27"\n',
    'StudioTheme panelAlt')
theme_path.write_text(theme, encoding='utf-8')

# WorkflowBar keeps the existing session/dialog implementation, but presentation becomes ribbon-native.
workflow_path = ROOT / 'apps/arstack_studio/qml/WorkflowBar.qml'
workflow = workflow_path.read_text(encoding='utf-8')
anchor = '''    function resetDockLayout() {
        controller.phasorDetached = false
        controller.waveformDetached = false
        controller.phasorDockVisible = true
        controller.waveformDockVisible = false
        controller.telemetryDockVisible = true
        controller.telemetryExpanded = false
        controller.showMessage("Dock layout reset.", false)
    }
'''
helpers = anchor + '''
    function openUpdatePrompt() {
        updatePromptDeferred = false
        updateDialog.open()
    }

    function openInstallPrompt() {
        installPromptDeferred = false
        installDialog.open()
    }

    function retryProfileSync() {
        if (smartSession.retryProfileSync())
            controller.showMessage("Retrying 4I+4V profile synchronization.", false)
        else
            controller.showMessage(smartSession.statusText, true)
    }
'''
workflow = replace_once(workflow, anchor, helpers, 'WorkflowBar ribbon helpers')
pattern = re.compile(r'\n    implicitHeight: 72\n    color: theme\.surface2\n    border\.color: theme\.line\n.*\n\}', re.S)
modern = '''
    implicitHeight: ribbon.compact ? 70 : 96
    color: "transparent"
    border.width: 0

    ModernRibbon {
        anchors.fill: parent
        theme: ribbon.theme
        controller: ribbon.controller
        workflow: ribbon
        session: smartSession
        device: ribbon.device
        uiFont: ribbon.uiFont
        compact: ribbon.compact
    }
}'''
workflow, count = pattern.subn(modern, workflow, count=1)
if count != 1:
    raise SystemExit(f'WorkflowBar presentation replacement count={count}')
workflow_path.write_text(workflow, encoding='utf-8')

# Main shell: denser desktop instrument proportions, transient-only toast, and 0 Hz = DC.
main_path = ROOT / 'apps/arstack_studio/qml/Main.qml'
main = main_path.read_text(encoding='utf-8')
main = replace_once(main,
    '    width: 1480\n    height: 900\n    minimumWidth: 1080\n    minimumHeight: 720\n',
    '    width: 1240\n    height: 760\n    minimumWidth: 960\n    minimumHeight: 620\n',
    'Main default geometry')
main = replace_once(main,
    '    readonly property bool compactLayout: width < 1300\n',
    '    readonly property bool compactLayout: width < 1120\n',
    'Main compact breakpoint')
main = replace_once(main,
    '    readonly property string toastMessage: transientMessage.length ? transientMessage : device.lastError\n    readonly property bool toastError: transientMessage.length ? transientError : device.lastError.length > 0\n',
    '    readonly property string toastMessage: transientMessage\n    readonly property bool toastError: transientError\n',
    'Main transient toast policy')
main = replace_once(main,
    '            Layout.preferredHeight: 72\n            Layout.minimumHeight: 72\n',
    '            Layout.preferredHeight: workflowBar.implicitHeight\n            Layout.minimumHeight: workflowBar.implicitHeight\n',
    'Main ribbon height')
main = replace_once(main,
    '                                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "AC"; tone: root.signalFrequency > 0 ? "accent" : "normal"; implicitWidth: 46; onClicked: root.setWaveformMode("AC") }\n                                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "DC"; tone: root.signalFrequency === 0 ? "accent" : "normal"; implicitWidth: 46; onClicked: root.setWaveformMode("DC") }\n                                Rectangle { width: 1; height: 24; color: studioTheme.lineSoft }\n\n',
    '',
    'Remove AC/DC mode buttons')
main = replace_once(main,
    '                                            enabled: root.signalFrequency > 0\n                                            validator: DoubleValidator { bottom: 0.001; top: 1000.0; decimals: 3 }\n',
    '                                            enabled: true\n                                            validator: DoubleValidator { bottom: 0.0; top: 1000.0; decimals: 3 }\n',
    'Frequency accepts DC zero')
main = main.replace('root.showMessage("AC frequency must be greater than 0 and not exceed 1000 Hz.", true)',
                    'root.showMessage("Frequency must be within 0..1000 Hz (0 = DC).", true)')
main = main.replace('CalmButton { visible: root.signalFrequency > 0; theme: studioTheme; uiFont: root.uiFont; text: "50";',
                    'CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "50";', 1)
main = main.replace('CalmButton { visible: root.signalFrequency > 0; theme: studioTheme; uiFont: root.uiFont; text: "60";',
                    'CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "60";', 1)
quick_50 = '                                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "50"; implicitWidth: 44; onClicked: root.setFrequencyValue(50) }\n'
if quick_50 not in main:
    raise SystemExit('Quick frequency 50 button not found')
main = main.replace(quick_50,
    '                                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "0"; implicitWidth: 44; toolTipText: "DC"; onClicked: root.setFrequencyValue(0) }\n' + quick_50,
    1)
main_path.write_text(main, encoding='utf-8')

# Matrix density: desktop instrument rows, not dashboard cards.
matrix_path = ROOT / 'apps/arstack_studio/qml/SignalMatrix.qml'
matrix = matrix_path.read_text(encoding='utf-8')
matrix = replace_once(matrix,
    '                Layout.preferredHeight: matrix.compact ? 54 : 60\n',
    '                Layout.preferredHeight: matrix.compact ? 46 : 50\n',
    'SignalMatrix row density')
matrix_path.write_text(matrix, encoding='utf-8')

print('Phase 1 ribbon + compact UX patch applied successfully')
