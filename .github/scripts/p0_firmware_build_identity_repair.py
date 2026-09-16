from pathlib import Path
import subprocess

PATH = ".github/scripts/p0_firmware_build_identity_repair.py"


def historical_patch_source() -> str:
    commits = subprocess.check_output(
        ["git", "rev-list", "HEAD", "--", PATH], text=True
    ).splitlines()
    for commit in commits[1:]:
        try:
            candidate = subprocess.check_output(
                ["git", "show", f"{commit}:{PATH}"], text=True
            )
        except subprocess.CalledProcessError:
            continue
        if (
            candidate.count("replace_once(") > 10
            and "embedded/esp32p4_smv_injector/CMakeLists.txt" in candidate
            and ".github/workflows/esp32p4-smv-injector.yml" in candidate
        ):
            return candidate
    raise SystemExit("historical P0 patch implementation not found")


text = historical_patch_source()

old = '''if(NOT ARSTACK_FIRMWARE_BUILD_ID MATCHES "^[0-9a-f]{16}$")
    message(FATAL_ERROR "ARSTACK_FIRMWARE_BUILD_ID must be exactly 16 hexadecimal characters")
endif()'''
new = '''string(LENGTH "${ARSTACK_FIRMWARE_BUILD_ID}" ARSTACK_FIRMWARE_BUILD_ID_LENGTH)
if(NOT ARSTACK_FIRMWARE_BUILD_ID_LENGTH EQUAL 16 OR NOT ARSTACK_FIRMWARE_BUILD_ID MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "ARSTACK_FIRMWARE_BUILD_ID must be exactly 16 hexadecimal characters")
endif()'''
if old not in text:
    raise SystemExit("historical CMake validation block not found")
text = text.replace(old, new, 1)

# AGENTS.md: sourceCommit in the existing manifest remains the sole provenance
# source of truth. Do not mutate the firmware workflow or add parallel metadata.
start = text.index('replace_once(\n    ".github/workflows/esp32p4-smv-injector.yml",')
end = text.index('replace_once(\n    "apps/arstack_studio/src/DeviceController.hpp",', start)
text = text[:start] + text[end:]

old = '''    '    const QString version = object.value(QStringLiteral("version")).toString().trimmed();\\n    const QString buildId = object.value(QStringLiteral("buildId")).toString().trimmed().toLower();\\n    const QString sourceCommit = object.value(QStringLiteral("sourceCommit")).toString().trimmed().toLower();\\n    const QString revisionPolicy = object.value(QStringLiteral("chipRevisionPolicy")).toString().trimmed().toLower();\\n',
'''
new = '''    '    const QString version = object.value(QStringLiteral("version")).toString().trimmed();\\n    const QString sourceCommit = object.value(QStringLiteral("sourceCommit")).toString().trimmed().toLower();\\n    const QString buildId = sourceCommit.left(16);\\n    const QString revisionPolicy = object.value(QStringLiteral("chipRevisionPolicy")).toString().trimmed().toLower();\\n',
'''
if old not in text:
    raise SystemExit("historical manifest provenance block not found")
text = text.replace(old, new, 1)

exec(compile(text, "<p0-build-identity-source-only>", "exec"), {"__name__": "__main__"})
