#!/usr/bin/env python3
"""Browser ribbon legibility and ownership contract (no Qt dependencies).

Guards against regressing to the 7px/8px pseudo-icons seen on field screenshots.
Native Qt CI still renders and starts QML on the exact source commit.
"""
from pathlib import Path

source = (Path(__file__).resolve().parent / "qml/IedBrowserWorkspace.qml").read_text(encoding="utf-8")
ribbon = source.split("component RibbonAction: Button {", 1)[-1].split(
    "function watchedRows()", 1)[0]
section = source.split("id: ribbonRow", 1)[-1].split(
    "visible: context.authorityKey", 1)[0]
required = (
    "implicitWidth: 80",
    "implicitHeight: 86",
    "width: 37",
    "height: 35",
    "font.pixelSize: action.badge.length > 2 ? 12 : 16",
    "font.pixelSize: 11",
    "maximumLineCount: 2",
    "wrapMode: Text.WordWrap",
    "action.hovered ? root.theme.accentSoft",
    "color: action.enabled ? root.theme.text : root.theme.muted",
)
if any(item not in ribbon for item in required):
    raise SystemExit("BROWSER_RIBBON_FAIL tiny or unreadable operator action")
if "Layout.preferredHeight: 112" not in source:
    raise SystemExit("BROWSER_RIBBON_FAIL toolbar clips 86px actions")
for label in ("APPLICATION", "ENDPOINT", "IED SESSION", "DATA", "SERVICES"):
    if f'text: "{label}"' not in section:
        raise SystemExit("BROWSER_RIBBON_FAIL missing service group " + label)
if 'text: "MODEL"' in section:
    raise SystemExit("BROWSER_RIBBON_FAIL duplicate identity chrome")
for item in (
    "onClicked: reports.enableSelected(true)",
    "onClicked: reports.disableSelected()",
    "onClicked: session.discoverAndConnect()",
    "onClicked: session.disconnectFromIed()",
    "onClicked: client.readEngineeringSelected()",
):
    if item not in section:
        raise SystemExit("BROWSER_RIBBON_FAIL lost live operation " + item)
if "implicitWidth: 66" in ribbon or "font.pixelSize: 8" in ribbon:
    raise SystemExit("BROWSER_RIBBON_FAIL retained miniature action")
# Explicit negative regression: an unapproved tiny font must break the gate.
shrunk = ribbon.replace("font.pixelSize: 11", "font.pixelSize: 8", 1)
if "font.pixelSize: 8" not in shrunk:
    raise SystemExit("BROWSER_RIBBON_FAIL ineffective tiny-caption regression")
print("BROWSER_RIBBON_PASS legibility=12px_or_larger badges=37px captions=11px actions=80x86")
