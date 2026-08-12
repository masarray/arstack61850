from pathlib import Path


def patch(path: str, replacements: list[tuple[str, str, str]]) -> None:
    p = Path(path)
    s = p.read_text(encoding='utf-8')
    for old, new, label in replacements:
        count = s.count(old)
        if count != 1:
            raise RuntimeError(f'{path}:{label}: expected one anchor, got {count}')
        s = s.replace(old, new, 1)
    p.write_text(s, encoding='utf-8')


# ---------------------------------------------------------------------------
# Hosted control runtime hardening and exact command-termination wire shape.
# ---------------------------------------------------------------------------
patch('tools/static_ied_server.cpp', [
    (
'''struct HostControlObjectContext final {
    HostControlRuntime* runtime{};
    HostControl* control{};
''',
'''struct HostControlObjectContext final {
    ManifestModel* model{};
    HostControl* control{};
''',
'lifetime: stable model pointer'),
    (
'''[[nodiscard]] bool apply_host_control(
    HostControlRuntime& runtime,
    HostControl& ctl,
    const mms::MmsStaticDirectBooleanOperate& decoded) {
    if (runtime.model == nullptr || decoded.test) return true;
    return apply_live_data(
        *runtime.model,
''',
'''[[nodiscard]] bool apply_host_control(
    ManifestModel& model,
    HostControl& ctl,
    const mms::MmsStaticDirectBooleanOperate& decoded) {
    if (decoded.test) return true;
    return apply_live_data(
        model,
''',
'lifetime: authoritative model apply'),
    (
'''    if (ctx == nullptr || ctx->runtime == nullptr || ctx->control == nullptr ||
        access.association_id == 0U) return {false, 3U};
''',
'''    if (ctx == nullptr || ctx->model == nullptr || ctx->control == nullptr ||
        access.association_id == 0U) return {false, 3U};
''',
'lifetime: context validation'),
    (
'''        if (!apply_host_control(*ctx->runtime, host, decoded)) return {false, 10U};
''',
'''        if (!apply_host_control(*ctx->model, host, decoded)) return {false, 10U};
''',
'lifetime: context apply'),
    (
'''            ctx->runtime = &runtime;
            ctx->control = host.get();
''',
'''            ctx->model = &model;
            ctx->control = host.get();
''',
'lifetime: context initialization'),
    # Match the proven direct-control server's live Oper contract exactly:
    # Origin.orCat is MMS unsigned and orIdent is variable-length up to 64 bytes.
    (
'''    origin.children.push_back(field("orCat", mms::MmsTypeKind::integer, 8U));
    origin.children.push_back(field("orIdent", mms::MmsTypeKind::octet_string, 8U));
''',
'''    origin.children.push_back(field("orCat", mms::MmsTypeKind::unsigned_integer, 8U));
    origin.children.push_back(field("orIdent", mms::MmsTypeKind::octet_string, 64U));
''',
'exact Origin type'),
    (
'''        if (ctx->service == HostControlObjectContext::Service::sbow) {
            const auto decision = host.planner->select_with_value(client, sequence, now);
            return {decision.accepted(), decision.accepted() ? 0U : 3U};
        }
''',
'''        if (ctx->service == HostControlObjectContext::Service::sbow) {
            const auto decision = host.planner->select_with_value(client, sequence, now);
            if (decision.accepted()) {
                std::cout << "IEDSIM_EVENT kind=control_select association=" << access.association_id
                          << " object=" << host.manifest->domain << '/' << host.manifest->logical_node
                          << '.' << host.manifest->data_object << " model=4\\n";
                std::cout.flush();
            }
            return {decision.accepted(), decision.accepted() ? 0U : 3U};
        }
''',
'enhanced SBO selection evidence'),
    # CommandTermination is an ordinary MMS InformationReport with an explicit
    # variable-access reference to CO$...$Oper. RCB-style report framing does not
    # populate MmsInformationReport::variable_references and cannot correlate to
    # a pending enhanced-security operation.
    (
'''#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/services.hpp"
''',
'''#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"
''',
'control report codec include'),
    (
'''[[nodiscard]] bool encode_host_control_termination(
    const mms::MmsStaticConnectionRuntime& connection,
    HostControl& host,
    ConnectionBuffers& buffers,
    std::size_t& bytes_written) noexcept {
    bytes_written = 0U;
    if (connection.state() != mms::MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U || host.last_oper.empty()) return false;
    const std::array<std::uint8_t, 2U> opt{{0x04U, 0x00U}}; // dataRef
    const std::array<mms::MmsInformationReportReferenceInput, 1U> refs{{
        {host.manifest->domain, host.oper_item}}};
    const std::array<mms::MmsReadAccessResultInput, 1U> results{{
        {true, host.last_oper, 0U}}};
    mms::MmsInformationReportSnapshotInput report;
    report.report_id = "CommandTermination";
    report.optional_fields = opt;
    report.conf_revision = 1U;
    report.member_references = refs;
    report.member_results = results;
    report.reason_for_inclusion = 0x40U;
    const auto raw = mms::MmsInformationReportSpanCodec::encode_snapshot_into(
        report, buffers.report_response);
    if (!raw.success()) return false;
    const auto p_data = ar::iec61850::osi::PresentationSpanCodec::encode_p_data_into(
        std::span<const std::uint8_t>{buffers.report_response.data(), raw.bytes_written},
        buffers.report_workspace,
        connection.mms_presentation_context_id(),
        true);
''',
'''[[nodiscard]] bool encode_host_control_termination(
    const mms::MmsStaticConnectionRuntime& connection,
    HostControl& host,
    ConnectionBuffers& buffers,
    std::size_t& bytes_written) noexcept {
    bytes_written = 0U;
    if (connection.state() != mms::MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U || host.last_oper.empty()) return false;
    try {
        const auto values = mms::MmsDataCodec::decode_all(host.last_oper);
        if (values.size() != 1U) return false;
        mms::MmsInformationReport report;
        report.variable_references.push_back(
            mms::MmsObjectName::domain_specific(host.manifest->domain, host.oper_item));
        mms::MmsInformationReportItem item;
        item.index = 0U;
        item.value = values.front();
        report.items.push_back(std::move(item));
        const auto raw = mms::MmsInformationReportCodec::encode_pdu(report);
        if (raw.empty() || raw.size() > buffers.report_response.size()) return false;
        std::copy(raw.begin(), raw.end(), buffers.report_response.begin());
        const auto p_data = ar::iec61850::osi::PresentationSpanCodec::encode_p_data_into(
            std::span<const std::uint8_t>{buffers.report_response.data(), raw.size()},
            buffers.report_workspace,
            connection.mms_presentation_context_id(),
            true);
''',
'positive CommandTermination report shape'),
    (
'''    bytes_written = tpkt.bytes_written;
    return true;
}

[[nodiscard]] bool poll_host_control_terminations(
''',
'''        bytes_written = tpkt.bytes_written;
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool poll_host_control_terminations(
''',
'CommandTermination exception boundary'),
])


# ---------------------------------------------------------------------------
# GUI consistency: control model metadata is configuration, not a live process
# value. Also expose an exact MMS-item selector for deterministic QA/regression.
# ---------------------------------------------------------------------------
patch('apps/ied_simulator/src/IedSimulatorController.cpp', [
    (
'''    item.insert(QStringLiteral("origin"), QStringLiteral("scl"));
    item.insert(QStringLiteral("writable"), !value.quality && !value.timestamp);
    item.insert(QStringLiteral("mmsWritable"), value.mmsWritable);
''',
'''    item.insert(QStringLiteral("origin"), QStringLiteral("scl"));
    const auto controlMetadata =
        value.dataAttribute.compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) == 0 ||
        value.dataAttribute.compare(QStringLiteral("sboTimeout"), Qt::CaseInsensitive) == 0 ||
        value.dataAttribute.compare(QStringLiteral("operTimeout"), Qt::CaseInsensitive) == 0;
    item.insert(
        QStringLiteral("writable"),
        !value.quality && !value.timestamp && !controlMetadata);
    item.insert(QStringLiteral("mmsWritable"), value.mmsWritable);
''',
'control metadata GUI read-only'),
    (
'''void IedSimulatorController::selectValue(const int index) {
    const int normalized = index >= 0 && index < values_.size() ? index : -1;
    if (selectedValueIndex_ == normalized) return;
    selectedValueIndex_ = normalized;
    emit selectionChanged();
}

bool IedSimulatorController::startSimulation() {
''',
'''void IedSimulatorController::selectValue(const int index) {
    const int normalized = index >= 0 && index < values_.size() ? index : -1;
    if (selectedValueIndex_ == normalized) return;
    selectedValueIndex_ = normalized;
    emit selectionChanged();
}

bool IedSimulatorController::selectValueByMmsItem(const QString& mmsItem) {
    for (qsizetype index = 0; index < values_.size(); ++index) {
        if (values_.at(index).toMap().value(QStringLiteral("mmsItem")).toString() == mmsItem) {
            selectValue(static_cast<int>(index));
            return true;
        }
    }
    return false;
}

bool IedSimulatorController::startSimulation() {
''',
'deterministic MMS item selector'),
])


# ---------------------------------------------------------------------------
# QA CLI: retain --set-first-value compatibility but allow exact MMS item.
# ---------------------------------------------------------------------------
patch('apps/ied_simulator/src/main.cpp', [
    (
'''    const QCommandLineOption setFirstValueOption{
        QStringLiteral("set-first-value"),
        QStringLiteral("QA: apply a value to the first runtime point after start."),
        QStringLiteral("value")};
    const QCommandLineOption undoAfterOption{
''',
'''    const QCommandLineOption setFirstValueOption{
        QStringLiteral("set-first-value"),
        QStringLiteral("QA: apply a value to a runtime point after start."),
        QStringLiteral("value")};
    const QCommandLineOption setValueItemOption{
        QStringLiteral("set-value-item"),
        QStringLiteral("QA: select the runtime point by exact MMS item before applying --set-first-value."),
        QStringLiteral("mms-item")};
    const QCommandLineOption undoAfterOption{
''',
'QA item option declaration'),
    (
'''        portOption,
        setFirstValueOption,
        undoAfterOption,
''',
'''        portOption,
        setFirstValueOption,
        setValueItemOption,
        undoAfterOption,
''',
'QA item option registration'),
    (
'''        if (backend != nullptr && parser.isSet(setFirstValueOption)) {
            const auto value = parser.value(setFirstValueOption);
            auto* const applyTimer = new QTimer{backend};
            applyTimer->setInterval(100);
            QObject::connect(applyTimer, &QTimer::timeout, backend, [backend, applyTimer, value] {
                if (!backend->property("running").toBool()) return;
                QMetaObject::invokeMethod(backend, "selectValue", Q_ARG(int, 0));
                QMetaObject::invokeMethod(
                    backend,
                    "applySelectedValue",
                    Q_ARG(QString, value),
                    Q_ARG(QString, QStringLiteral("Good")),
                    Q_ARG(QString, QStringLiteral("Simulator QA")));
                applyTimer->stop();
                applyTimer->deleteLater();
            });
            applyTimer->start();
        }
''',
'''        if (backend != nullptr && parser.isSet(setFirstValueOption)) {
            const auto value = parser.value(setFirstValueOption);
            const auto targetItem = parser.value(setValueItemOption);
            auto* const applyTimer = new QTimer{backend};
            applyTimer->setInterval(100);
            QObject::connect(
                applyTimer,
                &QTimer::timeout,
                backend,
                [backend, applyTimer, value, targetItem] {
                    if (!backend->property("running").toBool()) return;
                    if (!targetItem.isEmpty()) {
                        bool selected = false;
                        QMetaObject::invokeMethod(
                            backend,
                            "selectValueByMmsItem",
                            Q_RETURN_ARG(bool, selected),
                            Q_ARG(QString, targetItem));
                        if (!selected) return;
                    } else {
                        QMetaObject::invokeMethod(backend, "selectValue", Q_ARG(int, 0));
                    }
                    QMetaObject::invokeMethod(
                        backend,
                        "applySelectedValue",
                        Q_ARG(QString, value),
                        Q_ARG(QString, QStringLiteral("Good")),
                        Q_ARG(QString, QStringLiteral("Simulator QA")));
                    applyTimer->stop();
                    applyTimer->deleteLater();
                });
            applyTimer->start();
        }
''',
'deterministic QA selection'),
])


# ---------------------------------------------------------------------------
# P1 regression explicitly targets the measurement point it validates.
# ---------------------------------------------------------------------------
patch('apps/ied_simulator/test_gui_live_value.py', [
    (
'''                "--set-first-value",
                "42",
                "--undo-after-ms",
''',
'''                "--set-first-value",
                "42",
                "--set-value-item",
                "TCTR1$MX$Amp$instMag$i",
                "--undo-after-ms",
''',
'deterministic P1 mutation target'),
])

print('P3 runtime + GUI/QA consistency repair applied')
