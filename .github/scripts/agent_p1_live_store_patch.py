from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    text = read(path)
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{path}: regex expected one match, got {count}: {pattern[:120]!r}")
    write(path, updated)


# ---------------------------------------------------------------------------
# Qt controller: server-authoritative live store, mirror events, deterministic
# state metadata, and undo through the same mutation channel.
# ---------------------------------------------------------------------------
hpp = "apps/ied_simulator/src/IedSimulatorController.hpp"
replace_once(
    hpp,
    "    Q_INVOKABLE void copyDiagnostics();\n    Q_INVOKABLE QString diagnosticsText() const;\n",
    "    Q_INVOKABLE void copyDiagnostics();\n    Q_INVOKABLE QString diagnosticsText() const;\n"
    "    Q_INVOKABLE bool writeLiveStateSnapshot(const QString& path) const;\n",
)
replace_once(
    hpp,
    "    struct ValueSnapshot final {\n"
    "        int iedIndex{-1};\n"
    "        int valueIndex{-1};\n"
    "        QVariantMap value;\n"
    "    };\n",
    "    struct ValueSnapshot final {\n"
    "        int iedIndex{-1};\n"
    "        int valueIndex{-1};\n"
    "        QVariantMap value;\n"
    "        quint64 appliedRevision{};\n"
    "    };\n",
)
replace_once(
    hpp,
    "    void processServerLine(const QString& line, bool standardError);\n"
    "    [[nodiscard]] bool writeModelManifest();\n",
    "    void processServerLine(const QString& line, bool standardError);\n"
    "    void applyServerValueState(const QVariantMap& fields);\n"
    "    [[nodiscard]] bool sendLiveMutation(\n"
    "        const QVariantMap& item,\n"
    "        const QString& value,\n"
    "        const QString& quality,\n"
    "        const QString& origin,\n"
    "        quint64 requestId,\n"
    "        std::optional<quint64> expectedRevision);\n"
    "    [[nodiscard]] bool writeModelManifest();\n",
)
replace_once(
    hpp,
    "    quint64 serverStartGeneration_{};\n"
    "    quint64 modelRevision_{};\n"
    "    bool running_{};\n",
    "    quint64 serverStartGeneration_{};\n"
    "    quint64 modelRevision_{};\n"
    "    quint64 nextMutationRequest_{1U};\n"
    "    quint64 pendingMutationRequest_{};\n"
    "    bool pendingUndo_{};\n"
    "    bool running_{};\n",
)

cpp = "apps/ied_simulator/src/IedSimulatorController.cpp"
replace_once(
    cpp,
    "#include <QGuiApplication>\n",
    "#include <QGuiApplication>\n#include <QJsonDocument>\n",
)
replace_once(
    cpp,
    "QByteArray manifestField(QString value) {\n"
    "    value.replace(QLatin1Char('\\t'), QLatin1Char(' '));\n"
    "    value.replace(QLatin1Char('\\r'), QLatin1Char(' '));\n"
    "    value.replace(QLatin1Char('\\n'), QLatin1Char(' '));\n"
    "    return value.toUtf8();\n"
    "}\n",
    "QByteArray manifestField(QString value) {\n"
    "    value.replace(QLatin1Char('\\t'), QLatin1Char(' '));\n"
    "    value.replace(QLatin1Char('\\r'), QLatin1Char(' '));\n"
    "    value.replace(QLatin1Char('\\n'), QLatin1Char(' '));\n"
    "    return value.toUtf8();\n"
    "}\n\n"
    "QString liveHex(const QString& value) {\n"
    "    return QString::fromLatin1(value.toUtf8().toHex());\n"
    "}\n\n"
    "QString liveUnhex(const QString& value) {\n"
    "    return QString::fromUtf8(QByteArray::fromHex(value.toLatin1()));\n"
    "}\n\n"
    "QString deterministicTimestamp(const quint64 milliseconds) {\n"
    "    return QDateTime::fromMSecsSinceEpoch(\n"
    "               static_cast<qint64>(milliseconds), Qt::UTC)\n"
    "        .toString(Qt::ISODateWithMs);\n"
    "}\n",
)
replace_once(
    cpp,
    "    item.insert(QStringLiteral(\"quality\"), QStringLiteral(\"Good\"));\n"
    "    item.insert(QStringLiteral(\"origin\"), QStringLiteral(\"Simulator\"));\n"
    "    item.insert(QStringLiteral(\"writable\"), !value.quality && !value.timestamp);\n"
    "    item.insert(QStringLiteral(\"mmsWritable\"), value.mmsWritable);\n"
    "    item.insert(QStringLiteral(\"changed\"), false);\n"
    "    item.insert(QStringLiteral(\"updated\"), QStringLiteral(\"—\"));\n",
    "    item.insert(QStringLiteral(\"quality\"), QStringLiteral(\"Good\"));\n"
    "    item.insert(QStringLiteral(\"origin\"), QStringLiteral(\"scl\"));\n"
    "    item.insert(QStringLiteral(\"writable\"), !value.quality && !value.timestamp);\n"
    "    item.insert(QStringLiteral(\"mmsWritable\"), value.mmsWritable);\n"
    "    item.insert(QStringLiteral(\"changed\"), false);\n"
    "    item.insert(QStringLiteral(\"timestamp\"), deterministicTimestamp(0U));\n"
    "    item.insert(QStringLiteral(\"updated\"), deterministicTimestamp(0U));\n"
    "    item.insert(QStringLiteral(\"liveRevision\"), 0ULL);\n",
)
replace_once(
    cpp,
    "        if (!append) {\n"
    "            if (running_ || starting_) stopSimulation();\n"
    "            documents_.clear();\n"
    "            runtimeValues_.clear();\n"
    "        }\n",
    "        if (running_ || starting_) stopSimulation();\n"
    "        if (!append) {\n"
    "            documents_.clear();\n"
    "            runtimeValues_.clear();\n"
    "        }\n",
)
replace_once(
    cpp,
    "        previousValue_.reset();\n"
    "        rebuildPresentation();\n",
    "        previousValue_.reset();\n"
    "        pendingMutationRequest_ = 0U;\n"
    "        pendingUndo_ = false;\n"
    "        rebuildPresentation();\n",
)
replace_once(
    cpp,
    "    previousValue_.reset();\n"
    "    sourceName_.clear();\n",
    "    previousValue_.reset();\n"
    "    pendingMutationRequest_ = 0U;\n"
    "    pendingUndo_ = false;\n"
    "    sourceName_.clear();\n",
)
regex_once(
    cpp,
    r"bool IedSimulatorController::applySelectedValue\(.*?\n\}\n\nbool IedSimulatorController::undoLastChange\(\) \{.*?\n\}\n\nvoid IedSimulatorController::clearActivity\(\) \{",
    r'''bool IedSimulatorController::applySelectedValue(
    const QString& value,
    const QString& quality,
    const QString& origin) {
    if (!running_ || pendingMutationRequest_ != 0U ||
        selectedValueIndex_ < 0 || selectedValueIndex_ >= values_.size()) {
        return false;
    }
    const auto item = values_.at(selectedValueIndex_).toMap();
    if (!item.value(QStringLiteral("writable")).toBool()) return false;

    previousValue_ = ValueSnapshot{
        selectedIedIndex_, selectedValueIndex_, item, 0U};
    const auto requestId = nextMutationRequest_++;
    pendingMutationRequest_ = requestId;
    pendingUndo_ = false;
    const auto expectedRevision = std::optional<quint64>{
        item.value(QStringLiteral("liveRevision")).toULongLong()};
    if (!sendLiveMutation(item, value, quality, origin, requestId, expectedRevision)) {
        pendingMutationRequest_ = 0U;
        previousValue_.reset();
        return false;
    }
    return true;
}

bool IedSimulatorController::undoLastChange() {
    if (!running_ || pendingMutationRequest_ != 0U || !previousValue_.has_value() ||
        previousValue_->iedIndex != selectedIedIndex_ ||
        previousValue_->valueIndex < 0 || previousValue_->valueIndex >= values_.size() ||
        previousValue_->appliedRevision == 0U) {
        return false;
    }
    const auto requestId = nextMutationRequest_++;
    pendingMutationRequest_ = requestId;
    pendingUndo_ = true;
    const auto& previous = previousValue_->value;
    if (!sendLiveMutation(
            previous,
            previous.value(QStringLiteral("value")).toString(),
            previous.value(QStringLiteral("quality")).toString(),
            QStringLiteral("gui-undo"),
            requestId,
            previousValue_->appliedRevision)) {
        pendingMutationRequest_ = 0U;
        pendingUndo_ = false;
        return false;
    }
    return true;
}

bool IedSimulatorController::sendLiveMutation(
    const QVariantMap& item,
    const QString& value,
    const QString& quality,
    const QString& origin,
    const quint64 requestId,
    const std::optional<quint64> expectedRevision) {
    if (serverProcess_.state() != QProcess::Running) return false;
    const auto domain = item.value(QStringLiteral("mmsDomain")).toString();
    const auto mmsItem = item.value(QStringLiteral("mmsItem")).toString();
    if (domain.isEmpty() || mmsItem.isEmpty()) return false;

    QByteArray command{"IEDSIM_CMD kind=set request="};
    command += QByteArray::number(requestId);
    command += " domain=";
    command += liveHex(domain).toLatin1();
    command += " item=";
    command += liveHex(mmsItem).toLatin1();
    command += " value=";
    command += liveHex(value).toLatin1();
    command += " quality=";
    command += liveHex(quality).toLatin1();
    command += " origin=";
    command += liveHex(origin).toLatin1();
    if (expectedRevision.has_value()) {
        command += " expected=";
        command += QByteArray::number(*expectedRevision);
    }
    command += '\n';
    return serverProcess_.write(command) == command.size();
}

void IedSimulatorController::applyServerValueState(const QVariantMap& fields) {
    const auto domain = liveUnhex(fields.value(QStringLiteral("domain")).toString());
    const auto itemName = liveUnhex(fields.value(QStringLiteral("item")).toString());
    const auto key = domain + QLatin1Char('\n') + itemName;
    auto found = runtimeValues_.find(key);
    if (found == runtimeValues_.end()) {
        appendActivity(
            QStringLiteral("Live state"),
            QStringLiteral("Server published an unknown state key %1/%2.").arg(domain, itemName),
            QStringLiteral("Warning"));
        return;
    }

    auto state = found.value();
    const auto revision = fields.value(QStringLiteral("revision")).toULongLong();
    const auto timestampMs = fields.value(QStringLiteral("timestamp_ms")).toULongLong();
    const auto timestamp = deterministicTimestamp(timestampMs);
    state.insert(QStringLiteral("value"), liveUnhex(fields.value(QStringLiteral("value")).toString()));
    state.insert(QStringLiteral("quality"), liveUnhex(fields.value(QStringLiteral("quality")).toString()));
    state.insert(QStringLiteral("origin"), liveUnhex(fields.value(QStringLiteral("origin")).toString()));
    state.insert(QStringLiteral("timestamp"), timestamp);
    state.insert(QStringLiteral("updated"), timestamp);
    state.insert(QStringLiteral("liveRevision"), revision);
    state.insert(QStringLiteral("changed"), revision != 0U);
    found.value() = state;

    for (qsizetype index = 0; index < values_.size(); ++index) {
        if (runtimeValueKey(values_.at(index).toMap()) == key) {
            values_[index] = state;
            break;
        }
    }

    const auto requestId = fields.value(QStringLiteral("request")).toULongLong();
    if (requestId != 0U && requestId == pendingMutationRequest_) {
        if (pendingUndo_) {
            previousValue_.reset();
        } else if (previousValue_.has_value()) {
            previousValue_->appliedRevision = revision;
        }
        pendingMutationRequest_ = 0U;
        pendingUndo_ = false;
    }

    emit valuesChanged();
    emit selectionChanged();
    appendActivity(
        QStringLiteral("Live state"),
        QStringLiteral("%1/%2 = %3 · quality %4 · origin %5 · rev %6")
            .arg(
                domain,
                itemName,
                state.value(QStringLiteral("value")).toString(),
                state.value(QStringLiteral("quality")).toString(),
                state.value(QStringLiteral("origin")).toString())
            .arg(revision),
        QStringLiteral("Success"));
}

bool IedSimulatorController::writeLiveStateSnapshot(const QString& path) const {
    if (path.isEmpty()) return false;
    QStringList keys = runtimeValues_.keys();
    std::sort(keys.begin(), keys.end());
    QVariantList snapshot;
    snapshot.reserve(keys.size());
    for (const auto& key : keys) snapshot.push_back(runtimeValues_.value(key));
    QSaveFile output{path};
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    output.write(QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Indented));
    return output.commit();
}

void IedSimulatorController::clearActivity() {''',
)
replace_once(
    cpp,
    "    const auto kind = fields.value(QStringLiteral(\"kind\")).toString();\n"
    "    if (kind == QStringLiteral(\"server_ready\")) {\n",
    "    const auto kind = fields.value(QStringLiteral(\"kind\")).toString();\n"
    "    if (kind == QStringLiteral(\"value_state\")) {\n"
    "        applyServerValueState(fields);\n"
    "        return;\n"
    "    }\n"
    "    if (kind == QStringLiteral(\"value_rejected\")) {\n"
    "        const auto requestId = fields.value(QStringLiteral(\"request\")).toULongLong();\n"
    "        const bool wasUndo = pendingUndo_;\n"
    "        if (requestId != 0U && requestId == pendingMutationRequest_) {\n"
    "            pendingMutationRequest_ = 0U;\n"
    "            pendingUndo_ = false;\n"
    "            if (!wasUndo) previousValue_.reset();\n"
    "        }\n"
    "        appendActivity(\n"
    "            QStringLiteral(\"Live state\"),\n"
    "            QStringLiteral(\"Mutation rejected: %1\")\n"
    "                .arg(liveUnhex(fields.value(QStringLiteral(\"reason\")).toString())),\n"
    "            QStringLiteral(\"Warning\"));\n"
    "        return;\n"
    "    }\n"
    "    if (kind == QStringLiteral(\"state_ready\")) {\n"
    "        appendActivity(\n"
    "            QStringLiteral(\"Live state\"),\n"
    "            QStringLiteral(\"Server-authoritative live store ready: %1 values, rev %2, clock %3 ms.\")\n"
    "                .arg(fields.value(QStringLiteral(\"values\")).toString())\n"
    "                .arg(fields.value(QStringLiteral(\"revision\")).toString())\n"
    "                .arg(fields.value(QStringLiteral(\"clock_ms\")).toString()),\n"
    "            QStringLiteral(\"Success\"));\n"
    "        return;\n"
    "    }\n"
    "    if (kind == QStringLiteral(\"server_ready\")) {\n",
)
replace_once(
    cpp,
    "        output.write(manifestField(item.value(QStringLiteral(\"value\")).toString()));\n"
    "        output.write(\"\\n\");\n"
    "    };\n",
    "        output.write(manifestField(item.value(QStringLiteral(\"value\")).toString()));\n"
    "        output.write(\"\\n\");\n"
    "        if (item.value(QStringLiteral(\"mmsWritable\")).toBool()) {\n"
    "            output.write(\"MUT\\t\");\n"
    "            output.write(manifestField(domain));\n"
    "            output.write(\"\\t\");\n"
    "            output.write(manifestField(mmsItem));\n"
    "            output.write(\"\\n\");\n"
    "        }\n"
    "    };\n",
)
replace_once(
    cpp,
    "            QStringLiteral(\"mmsItem\"),\n"
    "            QStringLiteral(\"options\")};\n",
    "            QStringLiteral(\"mmsItem\"),\n"
    "            QStringLiteral(\"timestamp\"),\n"
    "            QStringLiteral(\"liveRevision\"),\n"
    "            QStringLiteral(\"options\")};\n",
)
replace_once(
    cpp,
    "    item.insert(QStringLiteral(\"quality\"), QStringLiteral(\"Good\"));\n"
    "    item.insert(QStringLiteral(\"origin\"), QStringLiteral(\"Simulator\"));\n"
    "    item.insert(QStringLiteral(\"writable\"), !entry.is_quality && !entry.is_timestamp);\n",
    "    item.insert(QStringLiteral(\"quality\"), QStringLiteral(\"Good\"));\n"
    "    item.insert(QStringLiteral(\"origin\"), QStringLiteral(\"scl\"));\n"
    "    item.insert(QStringLiteral(\"writable\"), !entry.is_quality && !entry.is_timestamp);\n",
)
replace_once(
    cpp,
    "    item.insert(QStringLiteral(\"changed\"), false);\n"
    "    item.insert(QStringLiteral(\"updated\"), QStringLiteral(\"—\"));\n"
    "    if (type == QStringLiteral(\"Enumeration\")) {\n",
    "    item.insert(QStringLiteral(\"changed\"), false);\n"
    "    item.insert(QStringLiteral(\"timestamp\"), deterministicTimestamp(0U));\n"
    "    item.insert(QStringLiteral(\"updated\"), deterministicTimestamp(0U));\n"
    "    item.insert(QStringLiteral(\"liveRevision\"), 0ULL);\n"
    "    if (type == QStringLiteral(\"Enumeration\")) {\n",
)

# ---------------------------------------------------------------------------
# CLI QA hooks: state dump and deterministic undo scheduling.
# ---------------------------------------------------------------------------
main_cpp = "apps/ied_simulator/src/main.cpp"
replace_once(
    main_cpp,
    "    const QCommandLineOption exitAfterOption{\n"
    "        QStringLiteral(\"exit-after-ms\"),\n"
    "        QStringLiteral(\"QA: exit after the specified runtime duration.\"),\n"
    "        QStringLiteral(\"milliseconds\")};\n",
    "    const QCommandLineOption undoAfterOption{\n"
    "        QStringLiteral(\"undo-after-ms\"),\n"
    "        QStringLiteral(\"QA: invoke live-value undo after the specified delay.\"),\n"
    "        QStringLiteral(\"milliseconds\")};\n"
    "    const QCommandLineOption stateDumpOption{\n"
    "        QStringLiteral(\"state-dump\"),\n"
    "        QStringLiteral(\"Write the final Qt live-state mirror to JSON on exit.\"),\n"
    "        QStringLiteral(\"path\")};\n"
    "    const QCommandLineOption exitAfterOption{\n"
    "        QStringLiteral(\"exit-after-ms\"),\n"
    "        QStringLiteral(\"QA: exit after the specified runtime duration.\"),\n"
    "        QStringLiteral(\"milliseconds\")};\n",
)
replace_once(
    main_cpp,
    "        portOption,\n"
    "        setFirstValueOption,\n"
    "        exitAfterOption});\n",
    "        portOption,\n"
    "        setFirstValueOption,\n"
    "        undoAfterOption,\n"
    "        stateDumpOption,\n"
    "        exitAfterOption});\n",
)
replace_once(
    main_cpp,
    "        if (parser.isSet(screenshotOption)) {\n",
    "        if (backend != nullptr && parser.isSet(undoAfterOption)) {\n"
    "            bool valid{};\n"
    "            const auto milliseconds = parser.value(undoAfterOption).toInt(&valid);\n"
    "            if (valid && milliseconds > 0) {\n"
    "                QTimer::singleShot(milliseconds, backend, [backend] {\n"
    "                    QMetaObject::invokeMethod(backend, \"undoLastChange\");\n"
    "                });\n"
    "            }\n"
    "        }\n"
    "        if (backend != nullptr && parser.isSet(stateDumpOption)) {\n"
    "            const auto stateDumpPath = parser.value(stateDumpOption);\n"
    "            QObject::connect(\n"
    "                &app,\n"
    "                &QCoreApplication::aboutToQuit,\n"
    "                backend,\n"
    "                [backend, stateDumpPath] {\n"
    "                    QMetaObject::invokeMethod(\n"
    "                        backend,\n"
    "                        \"writeLiveStateSnapshot\",\n"
    "                        Q_ARG(QString, stateDumpPath));\n"
    "                });\n"
    "        }\n"
    "        if (parser.isSet(screenshotOption)) {\n",
)

# ---------------------------------------------------------------------------
# Host MMS server: authoritative live state, command queue, bounded MMS writes,
# deterministic logical clock, and state events back to Qt.
# ---------------------------------------------------------------------------
server = "tools/static_ied_server.cpp"
replace_once(server, "#include <chrono>\n", "#include <chrono>\n#include <deque>\n")
replace_once(server, "#include <limits>\n", "#include <limits>\n#include <mutex>\n")
replace_once(
    server,
    "std::atomic_bool g_stop{false};\n",
    "std::atomic_bool g_stop{false};\n"
    "std::mutex g_live_command_mutex;\n"
    "std::deque<std::string> g_live_commands;\n\n"
    "void start_live_command_reader() {\n"
    "    std::thread([] {\n"
    "        std::string line;\n"
    "        while (std::getline(std::cin, line)) {\n"
    "            if (!line.starts_with(\"IEDSIM_CMD \")) continue;\n"
    "            std::lock_guard lock{g_live_command_mutex};\n"
    "            g_live_commands.push_back(std::move(line));\n"
    "        }\n"
    "    }).detach();\n"
    "}\n\n"
    "[[nodiscard]] std::vector<std::string> take_live_commands() {\n"
    "    std::lock_guard lock{g_live_command_mutex};\n"
    "    std::vector<std::string> result;\n"
    "    result.reserve(g_live_commands.size());\n"
    "    while (!g_live_commands.empty()) {\n"
    "        result.push_back(std::move(g_live_commands.front()));\n"
    "        g_live_commands.pop_front();\n"
    "    }\n"
    "    return result;\n"
    "}\n",
)
replace_once(
    server,
    "struct ManifestValue final {\n",
    "struct ManifestValue final {\n",
)
replace_once(
    server,
    "    std::vector<std::uint8_t> encoded;\n"
    "    bool root{};\n"
    "};\n",
    "    std::vector<std::uint8_t> encoded;\n"
    "    std::string quality{\"Good\"};\n"
    "    std::string origin{\"scl\"};\n"
    "    std::uint64_t timestamp_ms{};\n"
    "    std::uint64_t live_revision{};\n"
    "    bool mms_writable{};\n"
    "    bool root{};\n"
    "};\n",
)
replace_once(
    server,
    "    std::vector<mms::MmsStaticDataSetEntry> data_sets;\n"
    "    std::size_t declared_entries{};\n"
    "};\n",
    "    std::vector<mms::MmsStaticDataSetEntry> data_sets;\n"
    "    std::uint64_t live_revision{};\n"
    "    std::uint64_t logical_time_ms{};\n"
    "    std::size_t declared_entries{};\n"
    "};\n\n"
    "ManifestModel* g_active_manifest_model{};\n",
)
replace_once(
    server,
    "[[nodiscard]] std::string upper_copy(std::string value) {\n",
    "[[nodiscard]] char hex_digit(const std::uint8_t value) noexcept {\n"
    "    return static_cast<char>(value < 10U ? '0' + value : 'a' + (value - 10U));\n"
    "}\n\n"
    "[[nodiscard]] std::string hex_encode(const std::string_view text) {\n"
    "    std::string result;\n"
    "    result.reserve(text.size() * 2U);\n"
    "    for (const auto ch : text) {\n"
    "        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(ch));\n"
    "        result.push_back(hex_digit(static_cast<std::uint8_t>((byte >> 4U) & 0x0FU)));\n"
    "        result.push_back(hex_digit(static_cast<std::uint8_t>(byte & 0x0FU)));\n"
    "    }\n"
    "    return result;\n"
    "}\n\n"
    "[[nodiscard]] std::uint8_t hex_value(const char ch) {\n"
    "    if (ch >= '0' && ch <= '9') return static_cast<std::uint8_t>(ch - '0');\n"
    "    if (ch >= 'a' && ch <= 'f') return static_cast<std::uint8_t>(10 + ch - 'a');\n"
    "    if (ch >= 'A' && ch <= 'F') return static_cast<std::uint8_t>(10 + ch - 'A');\n"
    "    throw std::runtime_error(\"Invalid live-state hex field.\");\n"
    "}\n\n"
    "[[nodiscard]] std::string hex_decode(const std::string_view text) {\n"
    "    if ((text.size() % 2U) != 0U) throw std::runtime_error(\"Odd live-state hex field.\");\n"
    "    std::string result;\n"
    "    result.reserve(text.size() / 2U);\n"
    "    for (std::size_t index = 0U; index < text.size(); index += 2U) {\n"
    "        result.push_back(static_cast<char>(\n"
    "            (hex_value(text[index]) << 4U) | hex_value(text[index + 1U])));\n"
    "    }\n"
    "    return result;\n"
    "}\n\n"
    "[[nodiscard]] std::string upper_copy(std::string value) {\n",
)

# Insert authoritative live-store helpers immediately before manifest loading.
replace_once(
    server,
    "[[nodiscard]] ManifestModel load_manifest_model(\n",
    r'''[[nodiscard]] bool integer_text_valid(
    const std::string& text,
    const std::optional<std::uint32_t> bits,
    const bool is_unsigned) noexcept {
    const auto upper = upper_copy(text);
    if (!is_unsigned &&
        (upper == "INTERMEDIATE-STATE" || upper == "OFF" || upper == "ON" ||
         upper == "OPEN" || upper == "CLOSED" || upper == "BAD-STATE")) {
        return true;
    }
    try {
        std::size_t consumed{};
        if (is_unsigned) {
            if (!text.empty() && text.front() == '-') return false;
            const auto value = std::stoull(text, &consumed, 10);
            if (consumed != text.size()) return false;
            if (bits.has_value() && *bits < 64U) {
                const auto maximum = (std::uint64_t{1U} << *bits) - 1U;
                return value <= maximum;
            }
            return true;
        }
        const auto value = std::stoll(text, &consumed, 10);
        if (consumed != text.size()) return false;
        if (bits.has_value() && *bits > 0U && *bits < 64U) {
            const auto magnitude = std::int64_t{1} << (*bits - 1U);
            return value >= -magnitude && value <= magnitude - 1;
        }
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool live_text_valid(
    const mms::MmsTypeSpecification& type,
    const std::string& text) noexcept {
    switch (type.kind) {
    case mms::MmsTypeKind::boolean: {
        const auto upper = upper_copy(text);
        return upper == "TRUE" || upper == "FALSE" || upper == "1" || upper == "0" ||
            upper == "ON" || upper == "OFF" || upper == "OPEN" || upper == "CLOSED";
    }
    case mms::MmsTypeKind::integer:
        return integer_text_valid(text, type.size, false);
    case mms::MmsTypeKind::unsigned_integer:
        return integer_text_valid(text, type.size, true);
    case mms::MmsTypeKind::floating_point:
        try {
            std::size_t consumed{};
            static_cast<void>(std::stod(text, &consumed));
            return consumed == text.size();
        } catch (...) {
            return false;
        }
    case mms::MmsTypeKind::visible_string:
    case mms::MmsTypeKind::mms_string:
        return !type.size.has_value() || text.size() <= *type.size;
    default:
        return false;
    }
}

[[nodiscard]] bool live_data_compatible(
    const mms::MmsTypeSpecification& type,
    const mms::MmsDataValue& data) noexcept {
    switch (type.kind) {
    case mms::MmsTypeKind::boolean: return data.kind() == mms::MmsDataKind::boolean;
    case mms::MmsTypeKind::integer: return data.kind() == mms::MmsDataKind::integer;
    case mms::MmsTypeKind::unsigned_integer:
        return data.kind() == mms::MmsDataKind::unsigned_integer;
    case mms::MmsTypeKind::floating_point:
        return data.kind() == mms::MmsDataKind::floating_point;
    case mms::MmsTypeKind::visible_string:
        return data.kind() == mms::MmsDataKind::visible_string;
    case mms::MmsTypeKind::mms_string: return data.kind() == mms::MmsDataKind::mms_string;
    case mms::MmsTypeKind::bit_string: return data.kind() == mms::MmsDataKind::bit_string;
    case mms::MmsTypeKind::octet_string:
        return data.kind() == mms::MmsDataKind::octet_string;
    case mms::MmsTypeKind::utc_time: return data.kind() == mms::MmsDataKind::utc_time;
    case mms::MmsTypeKind::array: return data.kind() == mms::MmsDataKind::array;
    case mms::MmsTypeKind::structure: return data.kind() == mms::MmsDataKind::structure;
    default: return false;
    }
}

[[nodiscard]] std::string canonical_live_text(
    const ManifestValue& value,
    const mms::MmsDataValue& data,
    const std::string_view preferred) {
    if (!preferred.empty()) return std::string{preferred};
    if (upper_copy(value.normalized_type) == "ENUMERATION" &&
        data.kind() == mms::MmsDataKind::integer) {
        if (const auto* integer = std::get_if<std::int64_t>(&data.value())) {
            switch (*integer) {
            case 0: return "intermediate-state";
            case 1: return "off";
            case 2: return "on";
            case 3: return "bad-state";
            default: break;
            }
        }
    }
    return mms::MmsDataCodec::to_display_string(data);
}

void emit_live_state(const ManifestValue& value, const std::uint64_t request_id) {
    std::cout << "IEDSIM_EVENT kind=value_state request=" << request_id
              << " domain=" << hex_encode(value.domain)
              << " item=" << hex_encode(value.item)
              << " value=" << hex_encode(value.text)
              << " quality=" << hex_encode(value.quality)
              << " origin=" << hex_encode(value.origin)
              << " timestamp_ms=" << value.timestamp_ms
              << " revision=" << value.live_revision << '\n';
    std::cout.flush();
}

void emit_live_rejected(const std::uint64_t request_id, const std::string_view reason) {
    std::cout << "IEDSIM_EVENT kind=value_rejected request=" << request_id
              << " reason=" << hex_encode(reason) << '\n';
    std::cout.flush();
}

[[nodiscard]] bool apply_live_data(
    ManifestModel& model,
    const std::size_t value_index,
    mms::MmsDataValue data,
    const std::string_view preferred_text,
    std::string quality,
    std::string origin,
    const std::uint64_t request_id,
    const std::optional<std::uint64_t> expected_revision) {
    if (value_index >= model.values.size()) return false;
    auto& value = model.values[value_index];
    if (value.root || !live_data_compatible(value.type, data)) return false;
    if (expected_revision.has_value() && value.live_revision != *expected_revision) return false;

    ++model.live_revision;
    ++model.logical_time_ms;
    value.data = std::move(data);
    value.encoded = mms::MmsDataCodec::encode(*value.data);
    value.text = canonical_live_text(value, *value.data, preferred_text);
    value.quality = std::move(quality);
    value.origin = std::move(origin);
    value.timestamp_ms = model.logical_time_ms;
    value.live_revision = model.live_revision;
    rebuild_manifest_roots(model);
    emit_live_state(value, request_id);
    return true;
}

[[nodiscard]] mms::MmsStaticWriteResult write_manifest_value(
    void* context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    if (context == nullptr || g_active_manifest_model == nullptr) return {false, 10U};
    auto& value = *static_cast<ManifestValue*>(context);
    if (!value.mms_writable) return {false, 3U};
    try {
        const auto decoded = mms::MmsDataCodec::decode_all(encoded_data);
        if (decoded.size() != 1U || !live_data_compatible(value.type, decoded.front())) {
            return {false, 3U};
        }
        const auto found = g_active_manifest_model->value_indices.find(
            object_key(value.domain, value.item));
        if (found == g_active_manifest_model->value_indices.end()) return {false, 10U};
        if (!apply_live_data(
                *g_active_manifest_model,
                found->second,
                decoded.front(),
                {},
                "Good",
                "mms-write",
                0U,
                std::nullopt)) {
            return {false, 10U};
        }
        return {true, 0U};
    } catch (...) {
        return {false, 10U};
    }
}

struct LiveCommand final {
    std::uint64_t request{};
    std::string domain;
    std::string item;
    std::string value;
    std::string quality{"Good"};
    std::string origin{"gui"};
    std::optional<std::uint64_t> expected_revision;
};

[[nodiscard]] LiveCommand parse_live_command(const std::string& line) {
    if (!line.starts_with("IEDSIM_CMD ")) throw std::runtime_error("Invalid live command prefix.");
    std::map<std::string, std::string> fields;
    std::istringstream stream{line.substr(11U)};
    std::string token;
    while (stream >> token) {
        const auto separator = token.find('=');
        if (separator == std::string::npos || separator == 0U) continue;
        fields.emplace(token.substr(0U, separator), token.substr(separator + 1U));
    }
    if (fields["kind"] != "set") throw std::runtime_error("Unsupported live command kind.");
    LiveCommand command;
    command.request = std::stoull(fields.at("request"));
    command.domain = hex_decode(fields.at("domain"));
    command.item = hex_decode(fields.at("item"));
    command.value = hex_decode(fields.at("value"));
    command.quality = hex_decode(fields.at("quality"));
    command.origin = hex_decode(fields.at("origin"));
    if (const auto expected = fields.find("expected"); expected != fields.end()) {
        command.expected_revision = std::stoull(expected->second);
    }
    return command;
}

void drain_live_commands(ManifestModel& model) {
    for (const auto& line : take_live_commands()) {
        std::uint64_t request_id{};
        try {
            const auto command = parse_live_command(line);
            request_id = command.request;
            const auto found = model.value_indices.find(object_key(command.domain, command.item));
            if (found == model.value_indices.end()) {
                emit_live_rejected(request_id, "unknown-object");
                continue;
            }
            auto& value = model.values[found->second];
            if (command.expected_revision.has_value() &&
                value.live_revision != *command.expected_revision) {
                emit_live_rejected(request_id, "stale-revision");
                continue;
            }
            if (!live_text_valid(value.type, command.value)) {
                emit_live_rejected(request_id, "invalid-value");
                continue;
            }
            auto data = manifest_data(value.type, command.value);
            if (!apply_live_data(
                    model,
                    found->second,
                    std::move(data),
                    command.value,
                    command.quality,
                    command.origin,
                    request_id,
                    command.expected_revision)) {
                emit_live_rejected(request_id, "mutation-failed");
            }
        } catch (const std::exception& exception) {
            emit_live_rejected(request_id, exception.what());
        }
    }
}

[[nodiscard]] ManifestModel load_manifest_model(
''',
)
replace_once(
    server,
    "    std::set<std::pair<std::string, std::string>> unique_roots;\n"
    "    std::set<std::pair<std::string, std::string>> unique_objects;\n",
    "    std::set<std::pair<std::string, std::string>> unique_roots;\n"
    "    std::set<std::pair<std::string, std::string>> unique_objects;\n"
    "    std::set<std::pair<std::string, std::string>> writable_objects;\n",
)
replace_once(
    server,
    "        } else if (fields.size() >= 5U && fields[0] == \"DS\") {\n",
    "        } else if (fields.size() >= 3U && fields[0] == \"MUT\") {\n"
    "            if (!fields[1].empty() && !fields[2].empty()) {\n"
    "                writable_objects.emplace(fields[1], fields[2]);\n"
    "            }\n"
    "        } else if (fields.size() >= 5U && fields[0] == \"DS\") {\n",
)
replace_once(
    server,
    "        value.type_signature = parsed.type_signature;\n"
    "        value.text = parsed.text;\n"
    "        encode_manifest_value(value);\n",
    "        value.type_signature = parsed.type_signature;\n"
    "        value.text = parsed.text;\n"
    "        value.mms_writable = writable_objects.contains({parsed.domain, parsed.item});\n"
    "        encode_manifest_value(value);\n",
)
replace_once(
    server,
    "    model.objects.reserve(model.values.size());\n"
    "    for (auto& value : model.values) {\n"
    "        model.objects.push_back(mms::MmsStaticObjectEntry{\n"
    "            value.domain,\n"
    "            value.item,\n"
    "            value.type_specification,\n"
    "            read_manifest_value,\n"
    "            &value});\n"
    "    }\n",
    "    model.objects.reserve(model.values.size());\n"
    "    for (auto& value : model.values) {\n"
    "        mms::MmsStaticObjectEntry entry{\n"
    "            value.domain,\n"
    "            value.item,\n"
    "            value.type_specification,\n"
    "            read_manifest_value,\n"
    "            &value};\n"
    "        if (value.mms_writable && !value.root) {\n"
    "            entry.write = write_manifest_value;\n"
    "            entry.write_context = &value;\n"
    "        }\n"
    "        model.objects.push_back(entry);\n"
    "    }\n",
)
replace_once(
    server,
    "[[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {\n",
    "[[maybe_unused]] [[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {\n",
)
regex_once(
    server,
    r"    auto previous_state = runtime\.state\(\);\n    auto next_model_refresh = std::chrono::steady_clock::now\(\);\n(.*?)    while \(!g_stop\.load\(std::memory_order_relaxed\)\) \{\n        const auto now = std::chrono::steady_clock::now\(\);\n        if \(manifest_model != nullptr && now >= next_model_refresh\) \{.*?\n        \}\n        const auto result = session\.poll_once\(\);",
    r'''    auto previous_state = runtime.state();
\1    while (!g_stop.load(std::memory_order_relaxed)) {
        if (manifest_model != nullptr) drain_live_commands(*manifest_model);
        const auto result = session.poll_once();''',
)
replace_once(
    server,
    "        auto manifest_model = load_manifest_model(\n"
    "            options.model_manifest, manifest_type, manifest_value);\n",
    "        auto manifest_model = load_manifest_model(\n"
    "            options.model_manifest, manifest_type, manifest_value);\n"
    "        if (!manifest_model.objects.empty()) {\n"
    "            g_active_manifest_model = &manifest_model;\n"
    "            start_live_command_reader();\n"
    "        }\n",
)
replace_once(
    server,
    "                  << \" profile=iedscout\" << '\\n';\n"
    "        std::cout.flush();\n\n"
    "        std::size_t connection_count = 0U;\n",
    "                  << \" profile=iedscout\" << '\\n';\n"
    "        std::cout.flush();\n"
    "        if (g_active_manifest_model != nullptr) {\n"
    "            std::cout << \"IEDSIM_EVENT kind=state_ready values=\"\n"
    "                      << manifest_model.value_indices.size()\n"
    "                      << \" revision=\" << manifest_model.live_revision\n"
    "                      << \" clock_ms=\" << manifest_model.logical_time_ms << '\\n';\n"
    "            std::cout.flush();\n"
    "        }\n\n"
    "        std::size_t connection_count = 0U;\n",
)
replace_once(
    server,
    "        while (!g_stop.load(std::memory_order_relaxed) &&\n"
    "               (options.maximum_connections == 0U ||\n"
    "                connection_count < options.maximum_connections)) {\n"
    "            const auto readiness = wait_socket(listener, true, 200U);\n",
    "        while (!g_stop.load(std::memory_order_relaxed) &&\n"
    "               (options.maximum_connections == 0U ||\n"
    "                connection_count < options.maximum_connections)) {\n"
    "            if (g_active_manifest_model != nullptr) drain_live_commands(manifest_model);\n"
    "            const auto readiness = wait_socket(\n"
    "                listener, true, g_active_manifest_model != nullptr ? 25U : 200U);\n",
)

# ---------------------------------------------------------------------------
# Reusable MMS probe: add bounded Write support for P1 bidirectional evidence.
# ---------------------------------------------------------------------------
probe = "tools/mms_read_probe.cpp"
replace_once(probe, "#include <limits>\n", "#include <limits>\n#include <optional>\n")
replace_once(
    probe,
    "        << \"  --type          Read GetVariableAccessAttributes instead of the value.\\n\"\n",
    "        << \"  --type          Read GetVariableAccessAttributes instead of the value.\\n\"\n"
    "        << \"  --write-bool V  Write a Boolean value (true/false).\\n\"\n"
    "        << \"  --write-int V   Write a signed integer value.\\n\"\n"
    "        << \"  --write-uint V  Write an unsigned integer value.\\n\"\n",
)
replace_once(
    probe,
    "        bool type_only{};\n"
    "        while (argument < argc) {\n",
    "        bool type_only{};\n"
    "        std::optional<mms::MmsDataValue> write_value;\n"
    "        std::string write_display;\n"
    "        while (argument < argc) {\n",
)
replace_once(
    probe,
    "            if (option == \"--type\") {\n"
    "                type_only = true;\n"
    "                continue;\n"
    "            }\n"
    "            if (argument >= argc) throw std::invalid_argument(option + \" requires a value.\");\n"
    "            const std::string value = argv[argument++];\n"
    "            if (option == \"--domain\") {\n",
    "            if (option == \"--type\") {\n"
    "                type_only = true;\n"
    "                continue;\n"
    "            }\n"
    "            if (argument >= argc) throw std::invalid_argument(option + \" requires a value.\");\n"
    "            const std::string value = argv[argument++];\n"
    "            if (option == \"--write-bool\" || option == \"--write-int\" ||\n"
    "                option == \"--write-uint\") {\n"
    "                if (write_value.has_value()) {\n"
    "                    throw std::invalid_argument(\"Only one write value may be specified.\");\n"
    "                }\n"
    "                if (option == \"--write-bool\") {\n"
    "                    if (value == \"true\" || value == \"1\") {\n"
    "                        write_value = mms::MmsDataValue::boolean(true);\n"
    "                        write_display = \"true\";\n"
    "                    } else if (value == \"false\" || value == \"0\") {\n"
    "                        write_value = mms::MmsDataValue::boolean(false);\n"
    "                        write_display = \"false\";\n"
    "                    } else {\n"
    "                        throw std::invalid_argument(\"--write-bool expects true/false.\");\n"
    "                    }\n"
    "                } else if (option == \"--write-int\") {\n"
    "                    std::size_t consumed{};\n"
    "                    const auto parsed = std::stoll(value, &consumed, 10);\n"
    "                    if (consumed != value.size()) throw std::invalid_argument(\"Invalid signed integer.\");\n"
    "                    write_value = mms::MmsDataValue::integer(parsed);\n"
    "                    write_display = value;\n"
    "                } else {\n"
    "                    std::size_t consumed{};\n"
    "                    const auto parsed = std::stoull(value, &consumed, 10);\n"
    "                    if (consumed != value.size()) throw std::invalid_argument(\"Invalid unsigned integer.\");\n"
    "                    write_value = mms::MmsDataValue::unsigned_integer(parsed);\n"
    "                    write_display = value;\n"
    "                }\n"
    "                continue;\n"
    "            }\n"
    "            if (option == \"--domain\") {\n",
)
replace_once(
    probe,
    "        if (type_only && count != 1U) {\n"
    "            throw std::invalid_argument(\"--type cannot be combined with --count.\");\n"
    "        }\n",
    "        if (type_only && count != 1U) {\n"
    "            throw std::invalid_argument(\"--type cannot be combined with --count.\");\n"
    "        }\n"
    "        if (write_value.has_value() && (type_only || count != 1U)) {\n"
    "            throw std::invalid_argument(\"Write mode cannot be combined with --type or --count.\");\n"
    "        }\n",
)
replace_once(
    probe,
    "        if (type_only) {\n",
    "        if (write_value.has_value()) {\n"
    "            const auto invoke_id = session.association().next_invoke_id();\n"
    "            mms::MmsWriteRequest request;\n"
    "            request.invoke_id = invoke_id;\n"
    "            request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));\n"
    "            request.values.push_back(*write_value);\n"
    "            const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(\n"
    "                request, session.association().negotiated().presentation_context_id);\n"
    "            const auto exchange = session.association().exchange_confirmed(encoded, invoke_id);\n"
    "            if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {\n"
    "                throw std::runtime_error(\"Write did not return Confirmed-Response.\");\n"
    "            }\n"
    "            const auto response = mms::MmsServiceCodec::decode_write_response(\n"
    "                response_payload(exchange), invoke_id);\n"
    "            if (!response.all_success()) {\n"
    "                throw std::runtime_error(\"Write returned a failed AccessResult.\");\n"
    "            }\n"
    "            std::cout << \"MMS_WRITE reference=\" << domain << '/' << item\n"
    "                      << \" value=\" << write_display << '\\n';\n"
    "            session.disconnect();\n"
    "            return 0;\n"
    "        }\n\n"
    "        if (type_only) {\n",
)
replace_once(
    probe,
    "        std::cerr << \"MMS read probe failed: \" << exception.what() << '\\n';\n",
    "        std::cerr << \"MMS read/write probe failed: \" << exception.what() << '\\n';\n",
)

# ---------------------------------------------------------------------------
# Fixture: one SP leaf for bounded generic MMS Write without opening CO/control.
# ---------------------------------------------------------------------------
fixture = "tests/fixtures/scl/iedsim-full-model.scd"
replace_once(
    fixture,
    "      <DO name=\"Health\" type=\"HealthType\" />\n"
    "    </LNodeType>\n",
    "      <DO name=\"Health\" type=\"HealthType\" />\n"
    "      <DO name=\"SimCfg\" type=\"SimCfgType\" />\n"
    "    </LNodeType>\n",
)
replace_once(
    fixture,
    "    <DOType id=\"HealthType\" cdc=\"SPS\">\n"
    "      <DA name=\"stVal\" bType=\"BOOLEAN\" fc=\"ST\" />\n"
    "      <DA name=\"q\" bType=\"Quality\" fc=\"ST\" />\n"
    "    </DOType>\n",
    "    <DOType id=\"HealthType\" cdc=\"SPS\">\n"
    "      <DA name=\"stVal\" bType=\"BOOLEAN\" fc=\"ST\" />\n"
    "      <DA name=\"q\" bType=\"Quality\" fc=\"ST\" />\n"
    "    </DOType>\n"
    "    <DOType id=\"SimCfgType\" cdc=\"SPG\">\n"
    "      <DA name=\"setVal\" bType=\"BOOLEAN\" fc=\"SP\" />\n"
    "    </DOType>\n",
)

# ---------------------------------------------------------------------------
# End-to-end gate: P0 remains intact; P1 proves authoritative server state,
# reverse MMS mutation to Qt, deterministic metadata, write bounds, and undo.
# ---------------------------------------------------------------------------
test = "apps/ied_simulator/test_gui_live_value.py"
text = read(test)
text = text.replace(
    '"""Prove full SCL materialization and GUI-applied state are visible over MMS."""',
    '"""Prove P0 model fidelity plus the P1 server-authoritative live-value store."""',
)
text = text.replace("import argparse\n", "import argparse\nimport json\n")
text = text.replace(
    "def run_probe(read_probe: str, port: int, item: str, *, type_only: bool = False) -> subprocess.CompletedProcess[str]:\n",
    "def run_probe(\n"
    "    read_probe: str,\n"
    "    port: int,\n"
    "    item: str,\n"
    "    *,\n"
    "    type_only: bool = False,\n"
    "    write_option: str | None = None,\n"
    "    write_value: str | None = None,\n"
    ") -> subprocess.CompletedProcess[str]:\n",
)
text = text.replace(
    "    if type_only:\n        command.append(\"--type\")\n",
    "    if type_only:\n"
    "        command.append(\"--type\")\n"
    "    if write_option is not None:\n"
    "        if write_value is None:\n"
    "            raise ValueError(\"write_value is required with write_option\")\n"
    "        command.extend([write_option, write_value])\n",
)
# Replace the entire main() body for a deterministic P1 sequence.
main_pattern = re.compile(r"def main\(\) -> int:\n.*?\n\nif __name__ == \"__main__\":", re.S)
new_main = r'''def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--read-probe", required=True)
    parser.add_argument("--scl", required=True)
    args = parser.parse_args()
    read_probe = resolve_read_probe(args.read_probe)

    port = free_port()
    environment = dict(os.environ)
    environment["QT_QPA_PLATFORM"] = "offscreen"
    with tempfile.TemporaryDirectory() as temp_dir, tempfile.TemporaryFile(
        mode="w+t", encoding="utf-8"
    ) as app_log:
        state_dump = Path(temp_dir) / "live-state.json"
        app = subprocess.Popen(
            [
                args.app,
                "--scl",
                args.scl,
                "--port",
                str(port),
                "--runtime",
                "--set-first-value",
                "42",
                "--undo-after-ms",
                "3500",
                "--state-dump",
                str(state_dump),
                "--exit-after-ms",
                "6000",
            ],
            stdout=app_log,
            stderr=subprocess.STDOUT,
            text=True,
            env=environment,
            creationflags=creation_flags(),
        )
        last_error = "server not ready"
        try:
            manifest_path = (
                Path(tempfile.gettempdir()) / f"arstack-ied-simulator-{app.pid}.model"
            )
            manifest_deadline = time.monotonic() + 8.0
            manifest_text = ""
            while time.monotonic() < manifest_deadline:
                if app.poll() is not None:
                    last_error = f"application exited early with code {app.returncode}"
                    break
                try:
                    manifest_text = manifest_path.read_text(encoding="utf-8")
                except (FileNotFoundError, PermissionError, UnicodeDecodeError):
                    manifest_text = ""
                p0_value_present = (
                    "TCTR1$MX$Amp$instMag$i\tINT16\tNumber\tinteger:16\t0"
                    in manifest_text
                )
                ordered_range_present = (
                    "TCTR1$MX$Amp$instMag$range\tINT8U\tNumber\tunsigned-integer:8\t0"
                    in manifest_text
                )
                structural_only_leaf_present = (
                    "XCBR1$ST$Health$stVal\tBOOLEAN\tBoolean\tboolean\tfalse"
                    in manifest_text
                )
                writable_sp_present = (
                    "XCBR1$SP$SimCfg$setVal\tBOOLEAN\tBoolean\tboolean\tfalse"
                    in manifest_text
                    and "MUT\tMU01LD0\tXCBR1$SP$SimCfg$setVal" in manifest_text
                )
                data_set_present = (
                    "DS\tMU01LD0\tLLN0$dsSV\tMU01LD0\tTCTR1$MX$Amp$instMag$i"
                    in manifest_text
                )
                rcb_present = (
                    "RCB\tMU01LD0\tLLN0$RP$urcb01\t0\tMU01_LD0_URCB01\t"
                    "MU01LD0\tLLN0$dsSV\t7\t20\t1000\t0" in manifest_text
                )
                no_ln_scaffold = "\nLN\t" not in "\n" + manifest_text
                order_preserved = (
                    "TCTR1$MX$Amp$instMag$i" in manifest_text
                    and "TCTR1$MX$Amp$instMag$range" in manifest_text
                    and manifest_text.index("TCTR1$MX$Amp$instMag$i")
                    < manifest_text.index("TCTR1$MX$Amp$instMag$range")
                )
                if (
                    manifest_text.startswith("ARSTACK_IED_MODEL\t4\t1\n")
                    and p0_value_present
                    and ordered_range_present
                    and structural_only_leaf_present
                    and writable_sp_present
                    and data_set_present
                    and rcb_present
                    and no_ln_scaffold
                    and order_preserved
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("P0 manifest regression: exact v4 structural model not published")

            # GUI -> authoritative server store -> MMS Read. The manifest must remain
            # at revision 1 with its initial value, proving live mutation is no longer
            # file-reload driven.
            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                edited_probe = run_probe(read_probe, port, "TCTR1$MX$Amp$instMag$i")
                if edited_probe.returncode == 0 and "value=42" in edited_probe.stdout:
                    break
                time.sleep(0.15)
            else:
                raise RuntimeError(
                    f"GUI -> server live mutation not visible: {edited_probe.stdout} {edited_probe.stderr}"
                )
            manifest_after_gui = manifest_path.read_text(encoding="utf-8")
            if not manifest_after_gui.startswith("ARSTACK_IED_MODEL\t4\t1\n") or (
                "TCTR1$MX$Amp$instMag$i\tINT16\tNumber\tinteger:16\t0"
                not in manifest_after_gui
            ):
                raise RuntimeError("GUI mutation unexpectedly rewrote the structural manifest")

            exact_type_probe = run_probe(
                read_probe, port, "TCTR1$MX$Amp$instMag$i", type_only=True
            )
            require_probe(exact_type_probe, "kind=integer size=16", "GVAA INT16 evidence")
            exact_unsigned_probe = run_probe(
                read_probe, port, "TCTR1$MX$Amp$instMag$range", type_only=True
            )
            require_probe(
                exact_unsigned_probe, "kind=unsigned size=8", "GVAA INT8U evidence"
            )

            # MMS Write is intentionally bounded to SCL FCs marked mmsWritable.
            # This is the real reverse-direction proof: MMS -> server store -> Qt mirror.
            writable_write = run_probe(
                read_probe,
                port,
                "XCBR1$SP$SimCfg$setVal",
                write_option="--write-bool",
                write_value="true",
            )
            require_probe(writable_write, "MMS_WRITE", "bounded SP MMS Write")
            writable_read = run_probe(read_probe, port, "XCBR1$SP$SimCfg$setVal")
            require_probe(writable_read, "value=true", "SP write readback")

            non_writable_write = run_probe(
                read_probe,
                port,
                "TCTR1$MX$Amp$instMag$i",
                write_option="--write-int",
                write_value="99",
            )
            if non_writable_write.returncode == 0:
                raise RuntimeError("MX leaf unexpectedly accepted generic MMS Write")
            still_edited = run_probe(read_probe, port, "TCTR1$MX$Amp$instMag$i")
            require_probe(still_edited, "value=42", "write-boundary preservation")

            structural_probe = run_probe(read_probe, port, "XCBR1$ST$Health$stVal")
            require_probe(structural_probe, "value=false", "structural-only leaf read")
            rcb_probe = run_probe(read_probe, port, "LLN0$RP$urcb01$RptID")
            require_probe(rcb_probe, "value=MU01_LD0_URCB01", "URCB RptID read")

            # QA undo is scheduled at 3.5 s and must travel through the same server
            # live store. Wait until MMS sees the restored initial value.
            undo_deadline = time.monotonic() + 5.0
            while time.monotonic() < undo_deadline:
                restored = run_probe(read_probe, port, "TCTR1$MX$Amp$instMag$i")
                if restored.returncode == 0 and "value=0" in restored.stdout:
                    break
                time.sleep(0.15)
            else:
                raise RuntimeError("Undo did not restore the server-authoritative value")

            app.wait(timeout=10)
            states = json.loads(state_dump.read_text(encoding="utf-8"))
            by_item = {state.get("mmsItem"): state for state in states}
            tctr = by_item["TCTR1$MX$Amp$instMag$i"]
            simcfg = by_item["XCBR1$SP$SimCfg$setVal"]

            expected_tctr = {
                "value": "0",
                "quality": "Good",
                "origin": "gui-undo",
                "timestamp": "1970-01-01T00:00:00.003Z",
                "liveRevision": 3,
            }
            expected_simcfg = {
                "value": "true",
                "quality": "Good",
                "origin": "mms-write",
                "timestamp": "1970-01-01T00:00:00.002Z",
                "liveRevision": 2,
            }
            for key, expected in expected_tctr.items():
                if tctr.get(key) != expected:
                    raise RuntimeError(f"Qt mirror TCTR {key}: {tctr.get(key)!r} != {expected!r}")
            for key, expected in expected_simcfg.items():
                if simcfg.get(key) != expected:
                    raise RuntimeError(
                        f"Qt mirror SimCfg {key}: {simcfg.get(key)!r} != {expected!r}"
                    )

            final_manifest = manifest_path.read_text(encoding="utf-8") if manifest_path.exists() else manifest_after_gui
            if not final_manifest.startswith("ARSTACK_IED_MODEL\t4\t1\n"):
                raise RuntimeError("Live state changed the structural manifest revision")

            print(
                "IEDSIM_P1_UNIFIED_LIVE_VALUE_PASS "
                "gui_to_mms=42 mms_to_qt=SP:true undo=0 "
                "clock_ms=1,2,3 origin=Simulator-QA,mms-write,gui-undo "
                "generic_write_bound=SP-only"
            )
            return 0
        except BaseException as exception:
            last_error = str(exception) or last_error
            if app.poll() is None:
                app.kill()
                app.wait(timeout=5)
            app_log.seek(0)
            output = app_log.read().strip()
            raise RuntimeError(
                f"GUI P1 live-value test failed: {last_error}; app_output={output}"
            ) from exception


if __name__ == "__main__":'''
text, count = main_pattern.subn(new_main, text, count=1)
if count != 1:
    raise RuntimeError("test_gui_live_value.py: failed to replace main")
write(test, text)

# Workflow text only: rename the acceptance step to describe P1.
workflow = ".github/workflows/ied-simulator-qt.yml"
replace_once(
    workflow,
    "      - name: Prove full SCL model and GUI value are visible through MMS\n",
    "      - name: Prove P0 model and P1 unified live-value store end to end\n",
)

print("P1 patch applied")
