from pathlib import Path

ROOT = Path('.')


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    if new in text:
        print(f'{label}: already applied')
        return
    if old not in text:
        raise SystemExit(f'{label}: expected source block not found')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')
    print(f'{label}: applied')


# 1) Real progress stays unknown until espflash emits telemetry.
replace_once(
    'apps/arstack_studio/src/FirmwareManager.cpp',
    '''    bootloaderHelpNeeded_ = false;\n    flashProgress_ = 0;\n    progressOutputTail_.clear();\n''',
    '''    bootloaderHelpNeeded_ = false;\n    // Do not invent 0%. Progress remains unknown until espflash emits real\n    // write telemetry. The UI renders this as an indeterminate write state.\n    flashProgress_ = -1;\n    progressOutputTail_.clear();\n''',
    'FirmwareManager unknown initial progress')

old_parser = '''int FirmwareManager::parseFlashProgress(const QString& output) {\n    // Keep this parser pure so the firmware contract harness can exercise the\n    // exact production token rules. Stream reassembly lives in\n    // updateProgressFromOutput() and remains bounded.\n    const QString normalized = normalizedTerminalProgress(output);\n    static const QRegularExpression expression{\n        QStringLiteral(R\"((?<![\\d.])(\\d{1,3})\\s*%)\")};\n    auto matches = expression.globalMatch(normalized);\n    int latest = -1;\n    while (matches.hasNext()) {\n        const int value = matches.next().captured(1).toInt();\n        if (value >= 0 && value <= 100) latest = value;\n    }\n    return latest;\n}\n'''
new_parser = '''int FirmwareManager::parseFlashProgress(const QString& output) {\n    // Keep this parser pure so deterministic fixtures exercise the exact\n    // production token rules. Stream reassembly remains bounded in\n    // updateProgressFromOutput(). espflash has used both percentage output and\n    // indicatif-style \"current/total segment ...\" progress bars.\n    const QString normalized = normalizedTerminalProgress(output);\n    int latest = -1;\n\n    static const QRegularExpression percentExpression{\n        QStringLiteral(R\"((?<![\\d.])(\\d{1,3})\\s*%)\")};\n    auto percentMatches = percentExpression.globalMatch(normalized);\n    while (percentMatches.hasNext()) {\n        const int value = percentMatches.next().captured(1).toInt();\n        if (value >= 0 && value <= 100) latest = value;\n    }\n\n    static const QRegularExpression segmentExpression{\n        QStringLiteral(R\"((?<!\\d)(\\d{1,9})\\s*/\\s*(\\d{1,9})\\s+segment\\b)\"),\n        QRegularExpression::CaseInsensitiveOption};\n    auto segmentMatches = segmentExpression.globalMatch(normalized);\n    while (segmentMatches.hasNext()) {\n        const auto match = segmentMatches.next();\n        bool currentOk = false;\n        bool totalOk = false;\n        const qint64 current = match.captured(1).toLongLong(&currentOk);\n        const qint64 total = match.captured(2).toLongLong(&totalOk);\n        if (!currentOk || !totalOk || total <= 0 || current < 0 || current > total) continue;\n        const int value = static_cast<int>((current * 100 + total / 2) / total);\n        latest = std::clamp(value, 0, 100);\n    }\n    return latest;\n}\n'''
replace_once(
    'apps/arstack_studio/src/FirmwareManager.cpp',
    old_parser,
    new_parser,
    'FirmwareManager espflash v4 progress parser')

# 2) Expose the supervisor's explicit firmware state to presentation.
replace_once(
    'apps/arstack_studio/src/SmartSessionController.hpp',
    '''    Q_PROPERTY(int firmwareProgress READ firmwareProgress NOTIFY stateChanged)\n    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY stateChanged)\n''',
    '''    Q_PROPERTY(int firmwareProgress READ firmwareProgress NOTIFY stateChanged)\n    Q_PROPERTY(QString firmwareUpdateStage READ firmwareUpdateStage NOTIFY stateChanged)\n    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY stateChanged)\n''',
    'SmartSession stage Q_PROPERTY')

replace_once(
    'apps/arstack_studio/src/SmartSessionController.hpp',
    '''    Q_ENUM(PortOwner)\n\n    explicit SmartSessionController(QObject* parent = nullptr);\n''',
    '''    Q_ENUM(PortOwner)\n\n    enum class UpdateStage {\n        idle,\n        stopping,\n        releasingPort,\n        probing,\n        flashing,\n        reconnecting,\n        waitingForBootloader,\n    };\n    Q_ENUM(UpdateStage)\n\n    explicit SmartSessionController(QObject* parent = nullptr);\n''',
    'SmartSession public UpdateStage')

replace_once(
    'apps/arstack_studio/src/SmartSessionController.hpp',
    '''    [[nodiscard]] int firmwareProgress() const noexcept;\n    [[nodiscard]] QString updateStatus() const;\n''',
    '''    [[nodiscard]] int firmwareProgress() const noexcept;\n    [[nodiscard]] QString firmwareUpdateStage() const {\n        switch (updateStage_) {\n        case UpdateStage::stopping:\n        case UpdateStage::releasingPort:\n            return QStringLiteral(\"prepare\");\n        case UpdateStage::probing:\n        case UpdateStage::waitingForBootloader:\n            return QStringLiteral(\"verify\");\n        case UpdateStage::flashing:\n            return QStringLiteral(\"write\");\n        case UpdateStage::reconnecting:\n            return QStringLiteral(\"reconnect\");\n        case UpdateStage::idle:\n            return QStringLiteral(\"idle\");\n        }\n        return QStringLiteral(\"idle\");\n    }\n    [[nodiscard]] QString updateStatus() const;\n''',
    'SmartSession stage projection')

replace_once(
    'apps/arstack_studio/src/SmartSessionController.hpp',
    '''    enum class UpdateStage {\n        idle,\n        stopping,\n        releasingPort,\n        probing,\n        flashing,\n        reconnecting,\n        waitingForBootloader,\n    };\n    enum class ProfileSyncStage { idle, deploying, failed };\n''',
    '''    enum class ProfileSyncStage { idle, deploying, failed };\n''',
    'Remove duplicate private UpdateStage')

# 3) QML consumes supervisor stage only. Write+reset are one truthful stage;
# reconnect+semantic verification remains the final stage.
replace_once(
    'apps/arstack_studio/qml/WorkflowBar.qml',
    '''    readonly property int firmwareStageIndex: {\n        if (smartSession.updateNeedsBootloaderHelp) return 1\n        if (!smartSession.updatingFirmware) return -1\n        if (FirmwareService.busy) {\n            if (!FirmwareService.targetVerified) return 1\n            if (FirmwareService.flashProgress >= 100) return 3\n            if (FirmwareService.flashProgress >= 0) return 2\n            return 1\n        }\n        return FirmwareService.flashProgress >= 100 ? 4 : 0\n    }\n    readonly property var firmwareStages: [\n        { title: \"Prepare session\", detail: \"Stop output and release the serial port\" },\n        { title: \"Verify board\", detail: \"Confirm ESP32-P4 ROM identity\" },\n        { title: \"Write firmware\", detail: \"Program the verified ARStack image\" },\n        { title: \"Restart board\", detail: \"Reset after the flash completes\" },\n        { title: \"Reconnect & verify\", detail: \"Confirm the new ARStack semantic identity\" }\n    ]\n''',
    '''    readonly property int firmwareStageIndex: {\n        switch (smartSession.firmwareUpdateStage) {\n        case \"prepare\": return 0\n        case \"verify\": return 1\n        case \"write\": return 2\n        case \"reconnect\": return 3\n        default: return -1\n        }\n    }\n    readonly property var firmwareStages: [\n        { title: \"Prepare session\", detail: \"Stop output and release the serial port\" },\n        { title: \"Verify board\", detail: \"Confirm ESP32-P4 ROM identity\" },\n        { title: \"Write & restart\", detail: \"Program the verified image and reset the board\" },\n        { title: \"Reconnect & verify\", detail: \"Confirm the new ARStack semantic identity\" }\n    ]\n''',
    'WorkflowBar supervisor-owned stages')

replace_once(
    'apps/arstack_studio/qml/WorkflowBar.qml',
    '''                        text: smartSession.firmwareProgress >= 0 ? smartSession.firmwareProgress + \"%\" : \"Starting…\"\n''',
    '''                        text: smartSession.firmwareProgress >= 0 ? smartSession.firmwareProgress + \"%\" : \"Starting write…\"\n''',
    'WorkflowBar unknown progress label')

old_track = '''                    Rectangle {\n                        height: parent.height\n                        radius: parent.radius\n                        color: ribbon.theme.accent\n                        width: smartSession.firmwareProgress < 0\n                            ? 0\n                            : parent.width * Math.max(0, Math.min(1, smartSession.firmwareProgress / 100.0))\n                        Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }\n                    }\n'''
new_track = '''                    Rectangle {\n                        visible: smartSession.firmwareProgress >= 0\n                        height: parent.height\n                        radius: parent.radius\n                        color: ribbon.theme.accent\n                        width: parent.width * Math.max(0, Math.min(1, smartSession.firmwareProgress / 100.0))\n                        Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }\n                    }\n                    Rectangle {\n                        id: indeterminateFlash\n                        visible: smartSession.firmwareProgress < 0\n                        height: parent.height\n                        width: Math.max(28, parent.width * 0.24)\n                        radius: parent.radius\n                        color: ribbon.theme.accent\n                        opacity: 0.72\n                        NumberAnimation on x {\n                            running: indeterminateFlash.visible\n                            loops: Animation.Infinite\n                            from: -indeterminateFlash.width\n                            to: flashTrack.width\n                            duration: 900\n                            easing.type: Easing.InOutQuad\n                        }\n                    }\n'''
replace_once(
    'apps/arstack_studio/qml/WorkflowBar.qml',
    old_track,
    new_track,
    'WorkflowBar indeterminate write progress')

# 4) Deterministic regressions: real espflash progress-bar format, arbitrary
# chunk split, monotonicity, and supervisor-owned stage projection.
replace_once(
    'apps/arstack_studio/src/DeterministicSessionHarness.cpp',
    '''            {\"optional bootloader wait -> cancel restores supervisor\", optionalBootloaderWaitCanCancel()},\n            {\"trusted ESP32-P4 identity timeout -> firmware required\", automaticIdentityTimeoutOffersFirmwareRecovery()},\n''',
    '''            {\"optional bootloader wait -> cancel restores supervisor\", optionalBootloaderWaitCanCancel()},\n            {\"firmware stage projection -> supervisor-owned\", firmwareStageProjectionIsExplicit()},\n            {\"fragmented espflash progress -> monotonic\", fragmentedEspflashProgressIsMonotonic()},\n            {\"trusted ESP32-P4 identity timeout -> firmware required\", automaticIdentityTimeoutOffersFirmwareRecovery()},\n''',
    'Harness P0 progress cases')

anchor = '''    static bool automaticIdentityTimeoutOffersFirmwareRecovery() {\n'''
insert = '''    static bool firmwareStageProjectionIsExplicit() {\n        Fixture fixture;\n        auto& session = fixture.session;\n        session.updateStage_ = SmartSessionController::UpdateStage::idle;\n        const bool idle = session.firmwareUpdateStage() == QStringLiteral(\"idle\");\n        session.updateStage_ = SmartSessionController::UpdateStage::stopping;\n        const bool stopping = session.firmwareUpdateStage() == QStringLiteral(\"prepare\");\n        session.updateStage_ = SmartSessionController::UpdateStage::releasingPort;\n        const bool releasing = session.firmwareUpdateStage() == QStringLiteral(\"prepare\");\n        session.updateStage_ = SmartSessionController::UpdateStage::probing;\n        const bool probing = session.firmwareUpdateStage() == QStringLiteral(\"verify\");\n        session.updateStage_ = SmartSessionController::UpdateStage::flashing;\n        const bool flashing = session.firmwareUpdateStage() == QStringLiteral(\"write\");\n        session.updateStage_ = SmartSessionController::UpdateStage::reconnecting;\n        const bool reconnecting = session.firmwareUpdateStage() == QStringLiteral(\"reconnect\");\n        session.updateStage_ = SmartSessionController::UpdateStage::waitingForBootloader;\n        const bool bootloader = session.firmwareUpdateStage() == QStringLiteral(\"verify\");\n        return idle && stopping && releasing && probing && flashing && reconnecting && bootloader;\n    }\n\n    static bool fragmentedEspflashProgressIsMonotonic() {\n        Fixture fixture;\n        auto& firmware = fixture.firmware;\n        firmware.operation_ = FirmwareManager::Operation::flash;\n        firmware.flashProgress_ = -1;\n        firmware.progressOutputTail_.clear();\n\n        firmware.updateProgressFromOutput(QString::fromLatin1(\"\\x1B[2K\\r[00:00:01] [================] 4\"));\n        const bool remainsUnknown = firmware.flashProgress_ == -1;\n        firmware.updateProgressFromOutput(QStringLiteral(\"2/100 segment 0x0\\r\"));\n        const bool reconstructsSplitCounter = firmware.flashProgress_ == 42;\n        firmware.updateProgressFromOutput(QStringLiteral(\"[00:00:02] [========] 21/100 segment 0x0\\r\"));\n        const bool neverMovesBackward = firmware.flashProgress_ == 42;\n        firmware.updateProgressFromOutput(QStringLiteral(\"[00:00:03] [========================================] 100/100 segment 0x0\\r\"));\n        const bool reachesCompletion = firmware.flashProgress_ == 100;\n\n        return remainsUnknown && reconstructsSplitCounter && neverMovesBackward && reachesCompletion &&\n            FirmwareManager::parseFlashProgress(QStringLiteral(\"Writing 64%\\r\")) == 64 &&\n            FirmwareManager::parseFlashProgress(QStringLiteral(\"[00:00:02] [================] 17/20 segment 0x10000\")) == 85 &&\n            FirmwareManager::parseFlashProgress(QStringLiteral(\"21/0 segment 0x0\")) == -1;\n    }\n\n'''
p = ROOT / 'apps/arstack_studio/src/DeterministicSessionHarness.cpp'
text = p.read_text(encoding='utf-8')
if 'fragmentedEspflashProgressIsMonotonic()' not in text.split(anchor)[0]:
    if anchor not in text:
        raise SystemExit('Harness insertion anchor not found')
    text = text.replace(anchor, insert + anchor, 1)
    p.write_text(text, encoding='utf-8')
    print('Harness P0 progress functions: applied')
else:
    print('Harness P0 progress functions: already applied')

print('P0 firmware progress finalization patch applied')
