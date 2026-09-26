#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 8:
    raise SystemExit(
        "usage: test_browser_control_ux.py "
        "IedBrowserWorkspace.qml BrowserControlDialog.qml "
        "MmsControlController.cpp IedBrowserSessionController.cpp "
        "MmsLiveTreeModel.cpp Main.qml CMakeLists.txt"
    )

workspace = Path(sys.argv[1]).read_text(encoding="utf-8")
dialog = Path(sys.argv[2]).read_text(encoding="utf-8")
controller = Path(sys.argv[3]).read_text(encoding="utf-8")
session = Path(sys.argv[4]).read_text(encoding="utf-8")
tree = Path(sys.argv[5]).read_text(encoding="utf-8")
main = Path(sys.argv[6]).read_text(encoding="utf-8")
cmake = Path(sys.argv[7]).read_text(encoding="utf-8")

def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise SystemExit(f"{label}: missing required token: {token}")

for token in (
    'text: "Control…"',
    'root.selectedModelNode.controlCandidate === true',
    'controlDialog.openFor(root.selectedModelNode)',
    'BrowserControlDialog {',
):
    require(workspace, token, "Browser workspace")

for token in (
    'Dialog {',
    'id: confirmDialog',
    'standardButtons: Dialog.Ok | Dialog.Cancel',
    'controls.select(',
    'controls.operate(',
    'controls.selectAndOperate(',
    'controls.cancel()',
    'Auto-select for Operate',
    'Interlock-check',
    'Synchro-check',
    'no automatic retry',
    'CommandTermination',
):
    require(dialog, token, "Control dialog")

for token in (
    'ControlDescriptorDiscovery::discover',
    'ControlObjectSession',
    'MmsAssociationControlTransport',
    'options.guarded_policy.authorize = authorizeExplicitOperatorAction',
    'parseControlValue',
    'request.auto_select = autoSelect',
    'select_with_value',
    'controlSession->cancel',
    'result.command_termination_received',
):
    require(controller, token, "Control controller")

for forbidden in (
    'encode_write_request',
    'MmsServiceCodec::encode_write',
    'build_operate(',
    'build_select_with_value(',
):
    if forbidden in controller:
        raise SystemExit(
            f"Control controller: GUI-local wire/control encoder forbidden: {forbidden}"
        )

for token in (
    'controls_->setEngineeringContext(engineeringContext_)',
    'controls_->disconnectFromIed()',
    'controls_->prepareObject(objectReference)',
):
    require(session, token, "Browser session ownership")

for token in (
    'QStringLiteral("objectReference")',
    'QStringLiteral("controlCandidate")',
    'node.functionalConstraint.compare(QStringLiteral("CO")',
):
    require(tree, token, "Canonical tree control projection")

# P6E turns the previous singleton into an independently owned controller
# per IED slot. Both shell shapes must still wire the proven P6B service.
if 'IedBrowserFleetController {' in main:
    for token in (
        'controls: root.browserFleet.controlsAt(index)',
        'session: root.browserFleet.sessionAt(index)',
        'context: root.browserFleet.contextAt(index)',
    ):
        require(main, token, "Per-IED application shell")
else:
    for token in (
        'MmsControlController {',
        'controls: controls',
    ):
        require(main, token, "Application shell")

for token in (
    'src/MmsControlController.cpp',
    'qml/BrowserControlDialog.qml',
):
    require(cmake, token, "CMake packaging")

if 'Timer {' in dialog:
    raise SystemExit("Control dialog: background Timer/polling is forbidden")

print(
    "P6B_BROWSER_CONTROL_PASS "
    "canonical_target=true descriptor_discovery=reused control_session=reused "
    "confirmation=true no_auto_retry=true command_termination=true polling=false"
)
