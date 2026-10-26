#!/usr/bin/env python3
"""Fail-closed source contract for M4 draft/legacy boundary.

This complements the native M4 state harness; it does not emulate Qt focus or hardware.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
QML = (ROOT / "apps/arstack_studio/qml/Main.qml").read_text(encoding="utf-8")


def block(start: str, end: str) -> str:
    assert start in QML and end in QML, f"Missing contract anchor: {start} / {end}"
    return QML.split(start, 1)[1].split(end, 1)[0]


class M4QmlSafetyContract(unittest.TestCase):
    def test_debounced_legacy_save_is_gated(self):
        save = block("id: saveScenarioTimer", "function adoptRunningBoardScenario")
        self.assertIn("!m4Pages.active", save)
        self.assertIn("!root.restoringScenario", save)

    def test_close_cannot_persist_active_mu_as_legacy(self):
        close = block("onClosing: function(close)", "FontLoader {")
        self.assertIn("saveScenarioTimer.stop()", close)
        self.assertIn("!m4Pages.active", close)

    def test_open_saves_legacy_before_mu_navigation(self):
        opened = block("function m4OpenDraft()", "function m4SelectPage(slot)")
        self.assertIn("m4LegacyDraft = before", opened)
        self.assertIn("saveScenarioTimer.stop()", opened)
        self.assertIn("scenarioSettings.lastScenario", opened)

    def test_navigation_commits_frequency_before_snapshot(self):
        nav = block("function m4SelectPage(slot)", "function m4CloseDraft()")
        self.assertLess(nav.index("commitFrequencyEditor()"), nav.index("m4Pages.select("))
        self.assertLess(nav.index("flushPendingEdits()"), nav.index("m4Pages.select("))

    def test_restore_clears_old_frequency_editor_state(self):
        restore = block("function restoreM4Draft(draft)", "function m4OpenDraft()")
        self.assertIn("frequencyField.frequencyDirty = false", restore)
        self.assertIn("frequencyField.invalidInput = false", restore)


if __name__ == "__main__":
    unittest.main()
