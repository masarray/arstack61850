from pathlib import Path

p = Path('tools/static_ied_server.cpp')
s = p.read_text(encoding='utf-8')

def once(old: str, new: str, label: str) -> None:
    global s
    count = s.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, got {count}')
    s = s.replace(old, new, 1)

once(
'''struct HostControlObjectContext final {
    HostControlRuntime* runtime{};
    HostControl* control{};
''',
'''struct HostControlObjectContext final {
    ManifestModel* model{};
    HostControl* control{};
''',
'control context model pointer')

once(
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
'authoritative model apply')

once(
'''    if (ctx == nullptr || ctx->runtime == nullptr || ctx->control == nullptr ||
        access.association_id == 0U) return {false, 3U};
''',
'''    if (ctx == nullptr || ctx->model == nullptr || ctx->control == nullptr ||
        access.association_id == 0U) return {false, 3U};
''',
'context validation')

once(
'''        if (!apply_host_control(*ctx->runtime, host, decoded)) return {false, 10U};
''',
'''        if (!apply_host_control(*ctx->model, host, decoded)) return {false, 10U};
''',
'context apply')

once(
'''            ctx->runtime = &runtime;
            ctx->control = host.get();
''',
'''            ctx->model = &model;
            ctx->control = host.get();
''',
'context initialization')

# Match the proven direct-control server's live Oper contract exactly: Origin.orCat
# is MMS unsigned and orIdent is a variable-length octet-string up to 64 bytes.
once(
'''    origin.children.push_back(field("orCat", mms::MmsTypeKind::integer, 8U));
    origin.children.push_back(field("orIdent", mms::MmsTypeKind::octet_string, 8U));
''',
'''    origin.children.push_back(field("orCat", mms::MmsTypeKind::unsigned_integer, 8U));
    origin.children.push_back(field("orIdent", mms::MmsTypeKind::octet_string, 64U));
''',
'origin exact live type')

# Add explicit enhanced-SBO selection evidence.
once(
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
'enhanced select evidence')

# CommandTermination is an ordinary MMS InformationReport with an explicit
# variable-access reference to CO$...$Oper. RCB report framing is valid MMS but
# does not populate MmsInformationReport::variable_references, so the command
# decoder cannot correlate it to a pending enhanced-security Oper.
once(
'''#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/services.hpp"
''',
'''#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"
''',
'control report codec include')

once(
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
'command termination report shape')

once(
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
'command termination exception boundary')

p.write_text(s, encoding='utf-8')
print('P3 lifetime/exact-type/control-termination repair applied')
