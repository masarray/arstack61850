from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, got {count}: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


test = "apps/ied_simulator/test_gui_live_value.py"
replace_once(test, '                "--undo-after-ms",\n                "20000",\n', '                "--undo-after-ms",\n                "15000",\n')
replace_once(test, '                "--exit-after-ms",\n                "28000",\n', '                "--exit-after-ms",\n                "24000",\n')
replace_once(test, "            # QA undo runs at 8 s and must mutate the same authoritative store.\n", "            # QA undo fires after both report trials and must mutate the same authoritative store.\n")
replace_once(test, "            undo_deadline = time.monotonic() + 9.0\n", "            undo_deadline = time.monotonic() + 18.0\n")
replace_once(test, "            app.wait(timeout=32)\n", "            app.wait(timeout=28)\n")

print("P2 BRCB acceptance timing repaired")
