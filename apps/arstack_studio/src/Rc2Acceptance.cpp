// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace {
struct Verdict final {
    bool pass{false};
    QStringList failures;
};

[[nodiscard]] QJsonObject objectAt(const QJsonObject& root, const char* key) {
    return root.value(QLatin1String(key)).toObject();
}

[[nodiscard]] qint64 integerAt(const QJsonObject& object, const char* key) {
    return object.value(QLatin1String(key)).toVariant().toLongLong();
}

[[nodiscard]] bool boolAt(const QJsonObject& object, const char* key) {
    return object.value(QLatin1String(key)).toBool(false);
}

[[nodiscard]] QString stringAt(const QJsonObject& object, const char* key) {
    return object.value(QLatin1String(key)).toString();
}

void require(Verdict& verdict, const bool condition, const QString& failure) {
    if (!condition) verdict.failures.push_back(failure);
}

[[nodiscard]] Verdict evaluate(const QJsonObject& root, const QString& expectedSha = {}) {
    Verdict verdict;
    const auto profile = objectAt(root, "profile");
    const auto telemetry = objectAt(root, "telemetry");
    const auto operations = objectAt(root, "operations");
    const auto capture = objectAt(root, "capture");
    const auto operatorState = objectAt(root, "operator");

    require(verdict, stringAt(root, "schema") == QStringLiteral("arstack.studio.rc2.v1"),
            QStringLiteral("schema must be arstack.studio.rc2.v1"));

    const QString candidateSha = stringAt(root, "candidateSha").trimmed().toLower();
    static const QRegularExpression sha40{QStringLiteral("^[0-9a-f]{40}$")};
    require(verdict, sha40.match(candidateSha).hasMatch(),
            QStringLiteral("candidateSha must be a full 40-character Git commit SHA"));
    if (!expectedSha.isEmpty()) {
        require(verdict, candidateSha == expectedSha.trimmed().toLower(),
                QStringLiteral("candidateSha does not match the expected RC head"));
    }

    require(verdict, integerAt(profile, "channels") == 8,
            QStringLiteral("profile must remain 4I+4V / 8 channels"));
    require(verdict, integerAt(profile, "publisherRate") == 4000,
            QStringLiteral("publisher rate must be exactly 4000 fps"));
    require(verdict, integerAt(profile, "asduCount") == 1,
            QStringLiteral("profile must use exactly 1 ASDU"));
    require(verdict, integerAt(profile, "payloadBytes") == 64,
            QStringLiteral("sample payload must be exactly 64 bytes"));
    require(verdict, integerAt(profile, "channelLeafCount") == 16,
            QStringLiteral("profile must contain exactly 16 ordered FCDA leaves"));

    require(verdict, integerAt(telemetry, "durationSeconds") >= 3600,
            QStringLiteral("sustained telemetry soak must be at least 3600 seconds"));
    require(verdict, integerAt(telemetry, "fpsMin") >= 3999 && integerAt(telemetry, "fpsMax") <= 4001,
            QStringLiteral("observed one-second fps windows must stay inside 3999..4001"));
    require(verdict, integerAt(telemetry, "missed") == 0,
            QStringLiteral("missed sample slots must remain zero"));
    require(verdict, integerAt(telemetry, "txFailures") == 0,
            QStringLiteral("canonical TX failures must remain zero"));
    require(verdict, integerAt(telemetry, "healthReconnects") == 0,
            QStringLiteral("control health must not reconnect during the retained run"));
    require(verdict, integerAt(telemetry, "automaticStarts") == 0,
            QStringLiteral("no automatic Start is permitted during RC2"));

    require(verdict, boolAt(operations, "deployReady"),
            QStringLiteral("4I+4V deploy must reach READY"));
    require(verdict, integerAt(operations, "startStopCycles") >= 10,
            QStringLiteral("at least 10 clean Start/Stop cycles are required"));
    require(verdict, boolAt(operations, "zero"),
            QStringLiteral("Zero operation must pass"));
    require(verdict, boolAt(operations, "liveMagnitude"),
            QStringLiteral("live magnitude edit must pass"));
    require(verdict, boolAt(operations, "livePhase"),
            QStringLiteral("live phase edit must pass"));
    require(verdict, boolAt(operations, "liveFrequency"),
            QStringLiteral("live frequency edit must pass"));
    require(verdict, boolAt(operations, "liveQuality"),
            QStringLiteral("live Quality edit must pass"));
    require(verdict, boolAt(operations, "ctSaturation"),
            QStringLiteral("CT saturation live control must pass"));
    require(verdict, boolAt(operations, "profileResync"),
            QStringLiteral("profile re-sync must return to READY"));
    require(verdict, boolAt(operations, "signalGenerationAdvanced"),
            QStringLiteral("live edits must advance signal generation"));
    require(verdict, !boolAt(operations, "smpCntRestartOnLiveEdit"),
            QStringLiteral("live edits must not restart smpCnt"));

    require(verdict, integerAt(capture, "durationSeconds") >= 10,
            QStringLiteral("independent packet capture window must be at least 10 seconds"));
    require(verdict, boolAt(capture, "destinationMacMatches"),
            QStringLiteral("capture destination MAC must match the deployed profile"));
    require(verdict, boolAt(capture, "appidMatches"),
            QStringLiteral("capture APPID must match the deployed profile"));
    require(verdict, boolAt(capture, "vlanPcpMatches"),
            QStringLiteral("capture VLAN/PCP must match the deployed profile"));
    require(verdict, boolAt(capture, "svIdMatches"),
            QStringLiteral("capture svID must match the deployed profile"));
    require(verdict, boolAt(capture, "confRevMatches"),
            QStringLiteral("capture confRev must match the deployed profile"));
    require(verdict, boolAt(capture, "asduCountMatches"),
            QStringLiteral("capture must prove exactly 1 ASDU"));
    require(verdict, boolAt(capture, "payloadBytesMatches"),
            QStringLiteral("capture must prove a 64-byte sample payload"));
    require(verdict, boolAt(capture, "smpCntContinuous"),
            QStringLiteral("capture must prove continuous smpCnt progression"));
    require(verdict, boolAt(capture, "counterWrapObserved"),
            QStringLiteral("capture must observe the 3999 -> 0 counter wrap"));
    require(verdict, integerAt(capture, "smpSynchAdvertised") == 0,
            QStringLiteral("RC2 must retain honest smpSynch=0 advertisement"));

    require(verdict, boolAt(operatorState, "uiResponsive"),
            QStringLiteral("Studio UI must remain responsive during the retained run"));
    require(verdict, !boolAt(operatorState, "orphanWorkerObserved"),
            QStringLiteral("no orphan worker/process may remain after shutdown"));

    verdict.pass = verdict.failures.isEmpty();
    return verdict;
}

[[nodiscard]] QJsonObject passingFixture() {
    return {
        {QStringLiteral("schema"), QStringLiteral("arstack.studio.rc2.v1")},
        {QStringLiteral("candidateSha"), QStringLiteral("0123456789abcdef0123456789abcdef01234567")},
        {QStringLiteral("profile"), QJsonObject{
            {QStringLiteral("channels"), 8},
            {QStringLiteral("publisherRate"), 4000},
            {QStringLiteral("asduCount"), 1},
            {QStringLiteral("payloadBytes"), 64},
            {QStringLiteral("channelLeafCount"), 16}}},
        {QStringLiteral("telemetry"), QJsonObject{
            {QStringLiteral("durationSeconds"), 3600},
            {QStringLiteral("fpsMin"), 4000},
            {QStringLiteral("fpsMax"), 4000},
            {QStringLiteral("missed"), 0},
            {QStringLiteral("txFailures"), 0},
            {QStringLiteral("healthReconnects"), 0},
            {QStringLiteral("automaticStarts"), 0}}},
        {QStringLiteral("operations"), QJsonObject{
            {QStringLiteral("deployReady"), true},
            {QStringLiteral("startStopCycles"), 10},
            {QStringLiteral("zero"), true},
            {QStringLiteral("liveMagnitude"), true},
            {QStringLiteral("livePhase"), true},
            {QStringLiteral("liveFrequency"), true},
            {QStringLiteral("liveQuality"), true},
            {QStringLiteral("ctSaturation"), true},
            {QStringLiteral("profileResync"), true},
            {QStringLiteral("signalGenerationAdvanced"), true},
            {QStringLiteral("smpCntRestartOnLiveEdit"), false}}},
        {QStringLiteral("capture"), QJsonObject{
            {QStringLiteral("durationSeconds"), 10},
            {QStringLiteral("destinationMacMatches"), true},
            {QStringLiteral("appidMatches"), true},
            {QStringLiteral("vlanPcpMatches"), true},
            {QStringLiteral("svIdMatches"), true},
            {QStringLiteral("confRevMatches"), true},
            {QStringLiteral("asduCountMatches"), true},
            {QStringLiteral("payloadBytesMatches"), true},
            {QStringLiteral("smpCntContinuous"), true},
            {QStringLiteral("counterWrapObserved"), true},
            {QStringLiteral("smpSynchAdvertised"), 0}}},
        {QStringLiteral("operator"), QJsonObject{
            {QStringLiteral("uiResponsive"), true},
            {QStringLiteral("orphanWorkerObserved"), false}}}
    };
}

[[nodiscard]] bool rejectsMutation(QJsonObject fixture, const QString& section,
                                   const QString& key, const QJsonValue& value) {
    QJsonObject nested = fixture.value(section).toObject();
    nested.insert(key, value);
    fixture.insert(section, nested);
    return !evaluate(fixture).pass;
}

[[nodiscard]] bool selfTest() {
    const QJsonObject valid = passingFixture();
    if (!evaluate(valid, QStringLiteral("0123456789abcdef0123456789abcdef01234567")).pass) return false;

    return rejectsMutation(valid, QStringLiteral("telemetry"), QStringLiteral("durationSeconds"), 3599) &&
           rejectsMutation(valid, QStringLiteral("telemetry"), QStringLiteral("txFailures"), 1) &&
           rejectsMutation(valid, QStringLiteral("telemetry"), QStringLiteral("missed"), 1) &&
           rejectsMutation(valid, QStringLiteral("operations"), QStringLiteral("smpCntRestartOnLiveEdit"), true) &&
           rejectsMutation(valid, QStringLiteral("capture"), QStringLiteral("smpCntContinuous"), false) &&
           rejectsMutation(valid, QStringLiteral("capture"), QStringLiteral("payloadBytesMatches"), false) &&
           rejectsMutation(valid, QStringLiteral("capture"), QStringLiteral("smpSynchAdvertised"), 1) &&
           rejectsMutation(valid, QStringLiteral("operator"), QStringLiteral("uiResponsive"), false);
}

[[nodiscard]] QString argumentValue(int argc, char* argv[], const QString& name) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == name) return QString::fromLocal8Bit(argv[index + 1]);
    }
    return {};
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();

    if (arguments.contains(QStringLiteral("--self-test"))) {
        if (!selfTest()) {
            qCritical().noquote() << "RC2 acceptance policy self-test: FAIL";
            return 2;
        }
        qInfo().noquote() << "RC2 acceptance policy self-test: PASS · positive fixture + fail-closed negative mutations";
        return 0;
    }

    const QString evidencePath = argumentValue(argc, argv, QStringLiteral("--evidence"));
    const QString expectedSha = argumentValue(argc, argv, QStringLiteral("--expect-sha"));
    if (evidencePath.isEmpty()) {
        qCritical().noquote() << "Usage: arstack_rc2_acceptance --evidence <rc2.json> [--expect-sha <40-char SHA>]";
        return 3;
    }

    QFile file{evidencePath};
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical().noquote() << "RC2 evidence could not be opened:" << evidencePath;
        return 4;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCritical().noquote() << "RC2 evidence JSON is invalid:" << parseError.errorString();
        return 5;
    }

    const Verdict verdict = evaluate(document.object(), expectedSha);
    if (!verdict.pass) {
        qCritical().noquote() << "RC2 physical acceptance: FAIL";
        for (const QString& failure : verdict.failures) qCritical().noquote() << " -" << failure;
        return 6;
    }

    qInfo().noquote()
        << "RC2 physical acceptance: PASS · 4I+4V / 4000 fps / zero missed+TXfail / live controls / independent capture / smpSynch=0 truth boundary";
    return 0;
}
