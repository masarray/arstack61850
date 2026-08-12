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
        raise RuntimeError(f"{path}: expected one match, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    text = read(path)
    updated, count = re.subn(pattern, lambda _: replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{path}: regex expected one match, got {count}: {pattern!r}")
    write(path, updated)


# ---------------------------------------------------------------------------
# Preserve SCL TrgOps / OptFields semantics instead of inventing report policy.
# ---------------------------------------------------------------------------
model = "include/ariec61850/scl/model.hpp"
replace_once(
    model,
    "struct SclReportControl final {\n",
    "struct SclReportTriggerOptions final {\n"
    "    bool element_present{};\n"
    "    bool data_change{};\n"
    "    bool quality_change{};\n"
    "    bool data_update{};\n"
    "    bool integrity{};\n"
    "    bool general_interrogation{};\n\n"
    "    friend bool operator==(const SclReportTriggerOptions&, const SclReportTriggerOptions&) = default;\n"
    "};\n\n"
    "struct SclReportOptionalFields final {\n"
    "    bool element_present{};\n"
    "    bool sequence_number{};\n"
    "    bool report_timestamp{};\n"
    "    bool reason_code{};\n"
    "    bool data_set{};\n"
    "    bool data_reference{};\n"
    "    bool buffer_overflow{};\n"
    "    bool entry_id{};\n"
    "    bool configuration_revision{};\n"
    "    bool segmentation{};\n\n"
    "    friend bool operator==(const SclReportOptionalFields&, const SclReportOptionalFields&) = default;\n"
    "};\n\n"
    "struct SclReportControl final {\n",
)
replace_once(
    model,
    "    std::uint32_t integrity_period_milliseconds{};\n    std::vector<SclDataSetEntry> entries;\n",
    "    std::uint32_t integrity_period_milliseconds{};\n"
    "    SclReportTriggerOptions trigger_options;\n"
    "    SclReportOptionalFields optional_fields;\n"
    "    std::vector<SclDataSetEntry> entries;\n",
)

parser = "src/scl/parser_part_05_03.inc"
replace_once(
    parser,
    "                    report.integrity_period_milliseconds = uint_attribute(*control, \"intgPd\");\n"
    "                    if (binding.data_set != nullptr) {\n",
    "                    report.integrity_period_milliseconds = uint_attribute(*control, \"intgPd\");\n"
    "                    if (const auto* trigger = first_child(*control, \"TrgOps\"); trigger != nullptr) {\n"
    "                        report.trigger_options.element_present = true;\n"
    "                        report.trigger_options.data_change = bool_attribute(*trigger, \"dchg\");\n"
    "                        report.trigger_options.quality_change = bool_attribute(*trigger, \"qchg\");\n"
    "                        report.trigger_options.data_update = bool_attribute(*trigger, \"dupd\");\n"
    "                        report.trigger_options.integrity = bool_attribute(*trigger, \"period\");\n"
    "                        report.trigger_options.general_interrogation = bool_attribute(*trigger, \"gi\");\n"
    "                    }\n"
    "                    if (const auto* optional = first_child(*control, \"OptFields\"); optional != nullptr) {\n"
    "                        report.optional_fields.element_present = true;\n"
    "                        report.optional_fields.sequence_number = bool_attribute(*optional, \"seqNum\");\n"
    "                        report.optional_fields.report_timestamp = bool_attribute(*optional, \"timeStamp\");\n"
    "                        report.optional_fields.reason_code = bool_attribute(*optional, \"reasonCode\");\n"
    "                        report.optional_fields.data_set = bool_attribute(*optional, \"dataSet\");\n"
    "                        report.optional_fields.data_reference = bool_attribute(*optional, \"dataRef\");\n"
    "                        report.optional_fields.buffer_overflow = bool_attribute(*optional, \"bufOvfl\");\n"
    "                        report.optional_fields.entry_id = bool_attribute(*optional, \"entryID\");\n"
    "                        report.optional_fields.configuration_revision = bool_attribute(*optional, \"configRef\");\n"
    "                        report.optional_fields.segmentation = bool_attribute(*optional, \"segmentation\");\n"
    "                    }\n"
    "                    if (binding.data_set != nullptr) {\n",
)

# ---------------------------------------------------------------------------
# Carry report trigger/optional fields into the host manifest.
# ---------------------------------------------------------------------------
controller = "apps/ied_simulator/src/IedSimulatorController.cpp"
replace_once(
    controller,
    "QString deterministicTimestamp(const quint64 milliseconds) {\n"
    "    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(milliseconds))\n"
    "        .toUTC()\n"
    "        .toString(Qt::ISODateWithMs);\n"
    "}\n",
    "QString deterministicTimestamp(const quint64 milliseconds) {\n"
    "    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(milliseconds))\n"
    "        .toUTC()\n"
    "        .toString(Qt::ISODateWithMs);\n"
    "}\n\n"
    "quint8 reportTriggerMask(const ar::iec61850::scl::SclReportControl& report) {\n"
    "    quint8 mask{};\n"
    "    if (report.trigger_options.data_change) mask |= 0x40U;\n"
    "    if (report.trigger_options.quality_change) mask |= 0x20U;\n"
    "    if (report.trigger_options.data_update) mask |= 0x10U;\n"
    "    if (report.trigger_options.integrity) mask |= 0x08U;\n"
    "    if (report.trigger_options.general_interrogation) mask |= 0x04U;\n"
    "    return mask;\n"
    "}\n\n"
    "std::array<quint8, 2U> reportOptionalMask(\n"
    "    const ar::iec61850::scl::SclReportControl& report) {\n"
    "    std::array<quint8, 2U> mask{};\n"
    "    if (report.optional_fields.sequence_number) mask[0] |= 0x40U;\n"
    "    if (report.optional_fields.report_timestamp) mask[0] |= 0x20U;\n"
    "    if (report.optional_fields.reason_code) mask[0] |= 0x10U;\n"
    "    if (report.optional_fields.data_set) mask[0] |= 0x08U;\n"
    "    if (report.optional_fields.data_reference) mask[0] |= 0x04U;\n"
    "    if (report.buffered && report.optional_fields.buffer_overflow) mask[0] |= 0x02U;\n"
    "    if (report.buffered && report.optional_fields.entry_id) mask[0] |= 0x01U;\n"
    "    if (report.optional_fields.configuration_revision) mask[1] |= 0x80U;\n"
    "    if (report.buffered && report.optional_fields.segmentation) mask[1] |= 0x40U;\n"
    "    return mask;\n"
    "}\n",
)
replace_once(controller, "#include <algorithm>\n", "#include <algorithm>\n#include <array>\n")
replace_once(
    controller,
    "            output.write(report.indexed ? \"1\" : \"0\");\n"
    "            output.write(\"\\n\");\n",
    "            output.write(report.indexed ? \"1\" : \"0\");\n"
    "            const auto triggerMask = reportTriggerMask(report);\n"
    "            const auto optionalMask = reportOptionalMask(report);\n"
    "            output.write(\"\\t\");\n"
    "            output.write(QByteArray::number(triggerMask));\n"
    "            output.write(\"\\t\");\n"
    "            output.write(QByteArray::number(optionalMask[0]));\n"
    "            output.write(\"\\t\");\n"
    "            output.write(QByteArray::number(optionalMask[1]));\n"
    "            output.write(\"\\n\");\n",
)

# Fixture: integrity + GI plus all URCB-supported optional fields.
fixture = "tests/fixtures/scl/iedsim-full-model.scd"
replace_once(
    fixture,
    "            <ReportControl name=\"urcb01\" datSet=\"dsSV\" rptID=\"MU01_LD0_URCB01\" buffered=\"false\" bufTime=\"20\" intgPd=\"1000\" confRev=\"7\" indexed=\"false\" />",
    "            <ReportControl name=\"urcb01\" datSet=\"dsSV\" rptID=\"MU01_LD0_URCB01\" buffered=\"false\" bufTime=\"20\" intgPd=\"1000\" confRev=\"7\" indexed=\"false\">\n"
    "              <TrgOps period=\"true\" gi=\"true\" />\n"
    "              <OptFields seqNum=\"true\" timeStamp=\"true\" reasonCode=\"true\" dataSet=\"true\" dataRef=\"true\" configRef=\"true\" />\n"
    "            </ReportControl>",
)

# ---------------------------------------------------------------------------
# Build the existing portable URCB runtime/object/connection implementation in
# every server profile that compiles tools/static_ied_server.cpp.
# ---------------------------------------------------------------------------
for cmake in [
    "CMakeLists.txt",
    "apps/ied_simulator/CMakeLists.txt",
    "tools/static_ied_server_iedsim/CMakeLists.txt",
]:
    text = read(cmake)
    marker = "src/mms/information_report_span.cpp" if cmake == "CMakeLists.txt" else "information_report_span.cpp\"" if cmake.endswith("static_ied_server_iedsim/CMakeLists.txt") else "src/mms/information_report_span.cpp"
    if cmake == "CMakeLists.txt":
        old = "    src/mms/information_report_span.cpp\n    src/mms/buffered_selective_information_report_span.cpp\n"
        new = (
            "    src/mms/information_report_span.cpp\n"
            "    src/mms/static_urcb_runtime.cpp\n"
            "    src/mms/static_urcb_objects.cpp\n"
            "    src/mms/static_report_connection.cpp\n"
            "    src/mms/buffered_selective_information_report_span.cpp\n"
        )
    elif cmake == "apps/ied_simulator/CMakeLists.txt":
        old = (
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/information_report_span.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_object_table.cpp\n"
        )
        new = (
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/information_report_span.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_urcb_runtime.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_urcb_objects.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_report_connection.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_object_table.cpp\n"
        )
    else:
        old = (
            "    \"${ARSTACK_ROOT}/src/mms/information_report_span.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_object_table.cpp\"\n"
        )
        new = (
            "    \"${ARSTACK_ROOT}/src/mms/information_report_span.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_urcb_runtime.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_urcb_objects.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_report_connection.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_object_table.cpp\"\n"
        )
    replace_once(cmake, old, new)

# ---------------------------------------------------------------------------
# Server-side host adapter around existing MmsStaticUrcbRuntime.
# ---------------------------------------------------------------------------
server = "tools/static_ied_server.cpp"
replace_once(
    server,
    '#include "ariec61850/mms/static_server_session.hpp"\n',
    '#include "ariec61850/mms/static_server_session.hpp"\n'
    '#include "ariec61850/mms/static_report_connection.hpp"\n'
    '#include "ariec61850/mms/static_urcb_objects.hpp"\n'
    '#include "ariec61850/mms/static_urcb_runtime.hpp"\n',
)
replace_once(server, "#include <mutex>\n", "#include <mutex>\n#include <memory>\n")
replace_once(
    server,
    "struct ConnectionBuffers final {\n"
    "    std::array<std::uint8_t, 32'768U> receive{};\n"
    "    std::array<std::uint8_t, 32'768U> response{};\n"
    "    std::array<std::uint8_t, 8'192U> workspace{};\n"
    "};\n",
    "struct ConnectionBuffers final {\n"
    "    std::array<std::uint8_t, 32'768U> receive{};\n"
    "    std::array<std::uint8_t, 32'768U> response{};\n"
    "    std::array<std::uint8_t, 8'192U> workspace{};\n"
    "    std::array<std::uint8_t, 32'768U> report_response{};\n"
    "    std::array<std::uint8_t, 32'768U> report_workspace{};\n"
    "};\n",
)
replace_once(
    server,
    "struct ManifestDataSetStorage final {\n"
    "    std::string domain;\n"
    "    std::string item;\n"
    "    std::vector<std::pair<std::string, std::string>> member_names;\n"
    "    std::vector<mms::MmsStaticDataSetMember> members;\n"
    "};\n\n"
    "struct ManifestModel final {\n",
    "struct ManifestDataSetStorage final {\n"
    "    std::string domain;\n"
    "    std::string item;\n"
    "    std::vector<std::pair<std::string, std::string>> member_names;\n"
    "    std::vector<mms::MmsStaticDataSetMember> members;\n"
    "};\n\n"
    "struct ManifestReportControl final {\n"
    "    std::string domain;\n"
    "    std::string item;\n"
    "    bool buffered{};\n"
    "    std::string report_id;\n"
    "    std::string data_set_domain;\n"
    "    std::string data_set_item;\n"
    "    std::uint32_t conf_rev{1U};\n"
    "    std::uint32_t buffer_time_ms{};\n"
    "    std::uint32_t integrity_period_ms{};\n"
    "    bool indexed{};\n"
    "    std::uint8_t trigger_options{};\n"
    "    std::array<std::uint8_t, 2U> optional_fields{};\n"
    "};\n\n"
    "struct ManifestModel final {\n",
)
replace_once(
    server,
    "    std::vector<mms::MmsStaticDataSetEntry> data_sets;\n"
    "    std::uint64_t live_revision{};\n",
    "    std::vector<mms::MmsStaticDataSetEntry> data_sets;\n"
    "    std::vector<ManifestReportControl> report_controls;\n"
    "    std::uint64_t live_revision{};\n",
)
regex_once(
    server,
    r"    struct ParsedRcb final \{.*?    \};\n\n    std::vector<std::pair<std::string, std::string>> roots;",
    "    std::vector<std::pair<std::string, std::string>> roots;",
)
replace_once(
    server,
    "    std::vector<ParsedRcb> parsed_rcbs;\n",
    "    std::vector<ManifestReportControl> parsed_rcbs;\n",
)
replace_once(server, "            ParsedRcb rcb;\n", "            ManifestReportControl rcb;\n")
replace_once(
    server,
    "            rcb.indexed = fields[10] == \"1\";\n"
    "            if (!rcb.domain.empty() && !rcb.item.empty()) parsed_rcbs.push_back(std::move(rcb));\n",
    "            rcb.indexed = fields[10] == \"1\";\n"
    "            if (fields.size() >= 12U) {\n"
    "                try { rcb.trigger_options = static_cast<std::uint8_t>(std::stoul(fields[11])); } catch (...) {}\n"
    "            }\n"
    "            if (fields.size() >= 13U) {\n"
    "                try { rcb.optional_fields[0] = static_cast<std::uint8_t>(std::stoul(fields[12])); } catch (...) {}\n"
    "            }\n"
    "            if (fields.size() >= 14U) {\n"
    "                try { rcb.optional_fields[1] = static_cast<std::uint8_t>(std::stoul(fields[13])); } catch (...) {}\n"
    "            }\n"
    "            if (!rcb.domain.empty() && !rcb.item.empty()) parsed_rcbs.push_back(std::move(rcb));\n",
)
replace_once(
    server,
    "        ++model.declared_entries;\n"
    "    }\n\n"
    "    // Manifest v4 is object-driven: derive logical-node roots from the first\n",
    "        ++model.declared_entries;\n"
    "    }\n"
    "    model.report_controls = parsed_rcbs;\n\n"
    "    // Manifest v4 is object-driven: derive logical-node roots from the first\n",
)

runtime_code = r'''
struct HostUrcbControl final {
    ManifestReportControl* manifest{};
    std::vector<mms::MmsStaticObjectEntry> member_objects;
    std::array<mms::MmsStaticDataSetEntry, 1U> data_set_entries{};
    mms::MmsStaticObjectTable object_table{std::span<const mms::MmsStaticObjectEntry>{}};
    mms::MmsStaticDataSetTable data_set_table{};
    std::array<mms::MmsStaticUrcbDefinition, 1U> definitions{};
    std::array<mms::MmsStaticUrcbState, 1U> states{};
    std::unique_ptr<mms::MmsStaticUrcbRuntime> runtime;
    std::array<mms::MmsStaticObjectEntry,
        mms::MmsStaticUrcbObjectBank::attributes_per_control_block> bank_objects{};
    std::array<mms::MmsStaticUrcbObjectContext,
        mms::MmsStaticUrcbObjectBank::attributes_per_control_block> bank_contexts{};
    std::array<char, 4'096U> bank_names{};
    std::unique_ptr<mms::MmsStaticUrcbObjectBank> bank;
};

struct HostUrcbReporting final {
    std::chrono::steady_clock::time_point epoch{std::chrono::steady_clock::now()};
    std::vector<std::unique_ptr<HostUrcbControl>> controls;
};

[[nodiscard]] std::uint64_t report_now_ms(const void* raw) noexcept {
    const auto* reporting = static_cast<const HostUrcbReporting*>(raw);
    if (reporting == nullptr) return 0U;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - reporting->epoch).count();
    return elapsed <= 0 ? 0U : static_cast<std::uint64_t>(elapsed);
}

[[nodiscard]] std::array<std::uint8_t,
    mms::MmsInformationReportSpanCodec::binary_time_bytes> report_binary_time() noexcept {
    constexpr std::uint64_t kMillisecondsPerDay = 86'400'000ULL;
    constexpr std::uint64_t kUnixDaysTo1984 = 5'113ULL;
    const auto raw = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto milliseconds = raw <= 0 ? 0ULL : static_cast<std::uint64_t>(raw);
    const auto unix_days = milliseconds / kMillisecondsPerDay;
    const auto since_midnight = static_cast<std::uint32_t>(milliseconds % kMillisecondsPerDay);
    const auto days_1984 = static_cast<std::uint16_t>(std::min<std::uint64_t>(
        unix_days > kUnixDaysTo1984 ? unix_days - kUnixDaysTo1984 : 0ULL,
        std::numeric_limits<std::uint16_t>::max()));
    return {
        static_cast<std::uint8_t>((since_midnight >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((since_midnight >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((since_midnight >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(since_midnight & 0xFFU),
        static_cast<std::uint8_t>((days_1984 >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(days_1984 & 0xFFU)};
}

[[nodiscard]] std::string_view report_reason_text(
    const mms::MmsStaticUrcbReportReason reason) noexcept {
    switch (reason) {
    case mms::MmsStaticUrcbReportReason::general_interrogation: return "gi";
    case mms::MmsStaticUrcbReportReason::integrity: return "integrity";
    case mms::MmsStaticUrcbReportReason::none: return "none";
    }
    return "unknown";
}

[[nodiscard]] HostUrcbReporting initialize_host_urcb_reporting(ManifestModel& model) {
    HostUrcbReporting reporting;
    for (auto& report : model.report_controls) {
        if (report.buffered) continue;
        const auto data_set = std::find_if(
            model.data_sets.begin(), model.data_sets.end(), [&report](const auto& candidate) {
                return candidate.domain == report.data_set_domain &&
                    candidate.item == report.data_set_item;
            });
        if (data_set == model.data_sets.end() || data_set->members.empty()) {
            throw std::runtime_error(
                "URCB references a missing/empty DataSet: " + report.domain + "/" + report.item);
        }

        auto control = std::make_unique<HostUrcbControl>();
        control->manifest = &report;
        control->member_objects.reserve(data_set->members.size());
        for (const auto& member : data_set->members) {
            const auto object = std::find_if(
                model.objects.begin(), model.objects.end(), [&member](const auto& candidate) {
                    return candidate.domain == member.domain && candidate.item == member.item;
                });
            if (object == model.objects.end()) {
                throw std::runtime_error(
                    "URCB DataSet member is absent from the live object table.");
            }
            control->member_objects.push_back(*object);
        }
        if (control->member_objects.empty() ||
            control->member_objects.size() > mms::MmsStaticObjectTable::maximum_objects) {
            throw std::runtime_error(
                "URCB DataSet exceeds the bounded static reporting object profile.");
        }
        control->object_table = mms::MmsStaticObjectTable{control->member_objects};
        control->data_set_entries[0] = *data_set;
        control->data_set_table = mms::MmsStaticDataSetTable{control->data_set_entries};
        if (!control->object_table.valid() || !control->data_set_table.valid() ||
            !control->data_set_table.valid_against(control->object_table)) {
            throw std::runtime_error("URCB reporting subset failed strict static-table validation.");
        }

        if (report.report_id.empty()) {
            report.report_id = report.domain + "/" + report.item;
        }
        control->definitions[0] = mms::MmsStaticUrcbDefinition{
            report.domain,
            report.item,
            report.report_id,
            report.data_set_domain,
            report.data_set_item,
            report.conf_rev,
            report.optional_fields,
            report.buffer_time_ms,
            report.trigger_options,
            report.integrity_period_ms};
        control->runtime = std::make_unique<mms::MmsStaticUrcbRuntime>(
            control->definitions,
            control->states,
            control->object_table,
            control->data_set_table);
        if (!control->runtime->initialize()) {
            throw std::runtime_error(
                "MmsStaticUrcbRuntime rejected SCL report definition: " +
                report.domain + "/" + report.item);
        }
        control->bank = std::make_unique<mms::MmsStaticUrcbObjectBank>(
            *control->runtime,
            std::span<const mms::MmsStaticObjectEntry>{},
            control->bank_objects,
            control->bank_contexts,
            control->bank_names,
            report_now_ms,
            &reporting);
        if (!control->bank->initialize()) {
            throw std::runtime_error(
                "MmsStaticUrcbObjectBank failed to materialize dynamic RCB attributes.");
        }

        for (const auto& dynamic : control->bank->table().objects()) {
            const auto existing = std::find_if(
                model.objects.begin(), model.objects.end(), [&dynamic](const auto& candidate) {
                    return candidate.domain == dynamic.domain && candidate.item == dynamic.item;
                });
            if (existing == model.objects.end()) {
                if (model.objects.size() >= kHostMaximumManifestObjects) {
                    throw std::runtime_error("Dynamic URCB attributes exceed host object bound.");
                }
                model.objects.push_back(dynamic);
            } else {
                *existing = dynamic;
            }
        }
        reporting.controls.push_back(std::move(control));
    }
    return reporting;
}

[[nodiscard]] bool send_complete_report_frame(
    const embedded::TcpByteStream& stream,
    const std::span<const std::uint8_t> frame,
    std::size_t& total_sent) noexcept {
    std::size_t offset{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (offset < frame.size() && std::chrono::steady_clock::now() < deadline) {
        const auto sent = stream.send(frame.subspan(offset));
        if (sent.success() && sent.transferred != 0U) {
            offset += std::min(sent.transferred, frame.size() - offset);
            continue;
        }
        if (sent.status == embedded::IoStatus::would_block ||
            sent.status == embedded::IoStatus::timeout) {
            continue;
        }
        return false;
    }
    if (offset != frame.size()) return false;
    total_sent += frame.size();
    return true;
}

[[nodiscard]] bool poll_host_urcb_reports(
    HostUrcbReporting& reporting,
    const mms::MmsStaticConnectionRuntime& connection,
    const embedded::TcpByteStream& stream,
    ConnectionBuffers& buffers,
    const std::uint64_t association_id,
    const std::string_view remote,
    std::size_t& total_sent) {
    const auto now = report_now_ms(&reporting);
    const auto report_time = report_binary_time();
    for (auto& control : reporting.controls) {
        const auto result = mms::MmsStaticReportConnection::poll(
            connection,
            *control->runtime,
            now,
            report_time,
            buffers.report_response,
            buffers.report_workspace);
        if (result.status == mms::MmsStaticReportConnectionStatus::no_report_due ||
            result.status == mms::MmsStaticReportConnectionStatus::not_established) {
            continue;
        }
        if (!result.response_ready()) {
            std::cerr << "IEDSIM_EVENT kind=report_error association=" << association_id
                      << " rcb=" << control->manifest->domain << '/' << control->manifest->item
                      << " status=" << static_cast<unsigned>(result.status)
                      << " urcb_status=" << static_cast<unsigned>(result.urcb_status) << '\n';
            return false;
        }
        if (!send_complete_report_frame(
                stream,
                std::span<const std::uint8_t>{buffers.report_response.data(), result.bytes_written},
                total_sent)) {
            std::cerr << "IEDSIM_EVENT kind=report_send_error association=" << association_id
                      << " rcb=" << control->manifest->domain << '/' << control->manifest->item
                      << " remote=" << remote << '\n';
            return false;
        }
        std::cout << "IEDSIM_EVENT kind=report_sent association=" << association_id
                  << " remote=" << remote
                  << " rcb=" << control->manifest->domain << '/' << control->manifest->item
                  << " reason=" << report_reason_text(result.reason)
                  << " sequence=" << static_cast<unsigned>(result.sequence_number)
                  << " bytes=" << result.bytes_written << '\n';
        std::cout.flush();
        return true;
    }
    return true;
}

void reset_host_urcb_connection(
    HostUrcbReporting* reporting) noexcept {
    if (reporting == nullptr) return;
    const auto now = report_now_ms(reporting);
    for (auto& control : reporting->controls) {
        static_cast<void>(control->runtime->set_enabled(0U, false, now));
        static_cast<void>(control->runtime->set_reserved(0U, false));
    }
}

'''
replace_once(
    server,
    "[[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {\n",
    runtime_code + "[[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {\n",
)

# Pass reporting runtime into the established connection and send unsolicited
# report frames only when confirmed-response output is fully drained.
replace_once(
    server,
    "    ManifestModel* const manifest_model,\n"
    "    const std::uint64_t association_id,\n",
    "    ManifestModel* const manifest_model,\n"
    "    HostUrcbReporting* const reporting,\n"
    "    const std::uint64_t association_id,\n",
)
replace_once(
    server,
    "        if (result.terminal()) {\n"
    "            std::cout << \"IEDSIM_EVENT kind=client_closed association=\"\n",
    "        if (reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&\n"
    "            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&\n"
    "            !poll_host_urcb_reports(\n"
    "                *reporting, runtime, stream, buffers, association_id, remote, total_sent)) {\n"
    "            reset_host_urcb_connection(reporting);\n"
    "            runtime.close_transport();\n"
    "            return;\n"
    "        }\n"
    "        if (result.terminal()) {\n"
    "            reset_host_urcb_connection(reporting);\n"
    "            std::cout << \"IEDSIM_EVENT kind=client_closed association=\"\n",
)
replace_once(
    server,
    "    runtime.close_transport();\n}\n\n} // namespace\n",
    "    reset_host_urcb_connection(reporting);\n"
    "    runtime.close_transport();\n"
    "}\n\n} // namespace\n",
)

# Initialize runtime before constructing the final host object table so dynamic
# RCB entries replace the P0 inert attributes rather than creating duplicates.
replace_once(
    server,
    "        std::array<mms::MmsStaticObjectEntry, 13U> objects{};\n",
    "        auto urcb_reporting = initialize_host_urcb_reporting(manifest_model);\n\n"
    "        std::array<mms::MmsStaticObjectEntry, 13U> objects{};\n",
)
replace_once(
    server,
    "        if (g_active_manifest_model != nullptr) {\n"
    "            std::cout << \"IEDSIM_EVENT kind=state_ready values=\"\n",
    "        if (g_active_manifest_model != nullptr) {\n"
    "            std::cout << \"IEDSIM_EVENT kind=reporting_ready urcb=\"\n"
    "                      << urcb_reporting.controls.size()\n"
    "                      << \" runtime=static-urcb-core\" << '\\n';\n"
    "            std::cout.flush();\n"
    "            std::cout << \"IEDSIM_EVENT kind=state_ready values=\"\n",
)
replace_once(
    server,
    "                manifest_model.objects.empty() ? nullptr : &manifest_model,\n"
    "                static_cast<std::uint64_t>(connection_count),\n",
    "                manifest_model.objects.empty() ? nullptr : &manifest_model,\n"
    "                urcb_reporting.controls.empty() ? nullptr : &urcb_reporting,\n"
    "                static_cast<std::uint64_t>(connection_count),\n",
)

# ---------------------------------------------------------------------------
# Client trial evidence: print actual decoded report values so CI proves the
# InformationReport payload came from the P1 live backing value.
# ---------------------------------------------------------------------------
trial = "tools/static_rcb_trial.cpp"
replace_once(
    trial,
    '#include "ariec61850/mms/live_discovery.hpp"\n',
    '#include "ariec61850/mms/live_discovery.hpp"\n#include "ariec61850/mms/data_codec.hpp"\n',
)
replace_once(
    trial,
    "            std::cout << \"REPORT_EVIDENCE received=\"\n"
    "                      << active.subscription->received_reports\n"
    "                      << \" decodeFailures=\"\n"
    "                      << active.subscription->decode_failures\n"
    "                      << \" streams=\" << active.subscription->streams.size()\n"
    "                      << '\\n';\n\n"
    "            report_session.stop();\n",
    "            std::cout << \"REPORT_EVIDENCE received=\"\n"
    "                      << active.subscription->received_reports\n"
    "                      << \" decodeFailures=\"\n"
    "                      << active.subscription->decode_failures\n"
    "                      << \" streams=\" << active.subscription->streams.size()\n"
    "                      << '\\n';\n"
    "            for (const auto& stream : active.subscription->streams) {\n"
    "                if (stream.recent_frames.empty()) continue;\n"
    "                const auto& frame = stream.recent_frames.back();\n"
    "                for (const auto& report_value : frame.values) {\n"
    "                    if (!report_value.value.has_value()) continue;\n"
    "                    std::cout << \"REPORT_VALUE dataSetIndex=\"\n"
    "                              << report_value.data_set_index\n"
    "                              << \" ref=\" << report_value.data_reference\n"
    "                              << \" value=\"\n"
    "                              << mms::MmsDataCodec::to_display_string(*report_value.value)\n"
    "                              << '\\n';\n"
    "                }\n"
    "            }\n\n"
    "            report_session.stop();\n",
)

# ---------------------------------------------------------------------------
# Extend the P0/P1 E2E acceptance with RptEna + GI + integrity + actual report.
# ---------------------------------------------------------------------------
test = "apps/ied_simulator/test_gui_live_value.py"
replace_once(
    test,
    '"""Prove P0 model fidelity plus the P1 server-authoritative live-value store."""',
    '"""Prove P0 model, P1 unified live values, and P2 live reporting."""',
)
replace_once(
    test,
    "def resolve_read_probe(argument: str) -> str:\n",
    "def resolve_tool(argument: str, names: set[str], description: str) -> str:\n"
    "    path = Path(argument)\n"
    "    if path.is_file():\n"
    "        return str(path)\n"
    "    if path.is_dir():\n"
    "        matches = sorted(\n"
    "            candidate\n"
    "            for candidate in path.rglob(\"*\")\n"
    "            if candidate.is_file() and candidate.name in names\n"
    "        )\n"
    "        if matches:\n"
    "            return str(matches[0])\n"
    "    raise FileNotFoundError(f\"{description} not found under {path}\")\n\n\n"
    "def resolve_read_probe(argument: str) -> str:\n",
)
regex_once(
    test,
    r"def resolve_read_probe\(argument: str\) -> str:\n    path = Path\(argument\).*?    raise FileNotFoundError\(f\"MMS read probe not found under \{path\}\"\)\n",
    "def resolve_read_probe(argument: str) -> str:\n"
    "    return resolve_tool(\n"
    "        argument,\n"
    "        {\"ariec61850_mms_read_probe\", \"ariec61850_mms_read_probe.exe\"},\n"
    "        \"MMS read probe\",\n"
    "    )\n",
)
replace_once(
    test,
    "    parser.add_argument(\"--read-probe\", required=True)\n"
    "    parser.add_argument(\"--scl\", required=True)\n",
    "    parser.add_argument(\"--read-probe\", required=True)\n"
    "    parser.add_argument(\"--report-probe\", required=True)\n"
    "    parser.add_argument(\"--scl\", required=True)\n",
)
replace_once(
    test,
    "    read_probe = resolve_read_probe(args.read_probe)\n\n",
    "    read_probe = resolve_read_probe(args.read_probe)\n"
    "    report_probe = resolve_tool(\n"
    "        args.report_probe,\n"
    "        {\"ariec61850_static_rcb_trial\", \"ariec61850_static_rcb_trial.exe\"},\n"
    "        \"static RCB trial\",\n"
    "    )\n\n",
)
replace_once(test, '                "8000",\n', '                "10500",\n')
replace_once(test, '                "12000",\n', '                "15000",\n')
replace_once(
    test,
    "            exact_type_probe = run_probe(\n",
    "            report_trial = subprocess.run(\n"
    "                [\n"
    "                    report_probe,\n"
    "                    \"127.0.0.1\",\n"
    "                    str(port),\n"
    "                    \"--preferred-rcb\",\n"
    "                    \"MU01LD0/LLN0$RP$urcb01\",\n"
    "                    \"--dataset-ref\",\n"
    "                    \"MU01LD0/LLN0$dsSV\",\n"
    "                    \"--probe-cycles\",\n"
    "                    \"6\",\n"
    "                    \"--probe-delay-ms\",\n"
    "                    \"250\",\n"
    "                    \"--timeout-ms\",\n"
    "                    \"3000\",\n"
    "                    \"--arm\",\n"
    "                    \"IEC61850-LAB-STATIC-RCB\",\n"
    "                ],\n"
    "                capture_output=True,\n"
    "                text=True,\n"
    "                timeout=12,\n"
    "                check=False,\n"
    "                creationflags=creation_flags(),\n"
    "            )\n"
    "            if report_trial.returncode != 0 or \"SMART_STATIC_RCB_TRIAL_PASS\" not in report_trial.stdout:\n"
    "                raise RuntimeError(\n"
    "                    \"P2 static reporting trial failed: \"\n"
    "                    f\"exit={report_trial.returncode} stdout={report_trial.stdout} stderr={report_trial.stderr}\"\n"
    "                )\n"
    "            evidence = re.search(r\"REPORT_EVIDENCE received=(\\d+) decodeFailures=(\\d+)\", report_trial.stdout)\n"
    "            if evidence is None or int(evidence.group(1)) < 2 or int(evidence.group(2)) != 0:\n"
    "                raise RuntimeError(f\"P2 GI/integrity report evidence missing: {report_trial.stdout}\")\n"
    "            if \"REPORT_VALUE\" not in report_trial.stdout or \"value=42\" not in report_trial.stdout:\n"
    "                raise RuntimeError(f\"P2 report did not carry P1 live value 42: {report_trial.stdout}\")\n\n"
    "            exact_type_probe = run_probe(\n",
)
replace_once(test, "import os\n", "import os\nimport re\n")
replace_once(
    test,
    "            app.wait(timeout=16)\n",
    "            app.wait(timeout=19)\n",
)
replace_once(
    test,
    "            print(\n"
    "                \"IEDSIM_P1_UNIFIED_LIVE_VALUE_PASS \"\n",
    "            app_log.flush()\n"
    "            app_log.seek(0)\n"
    "            runtime_log = app_log.read()\n"
    "            if \"kind=report_sent\" not in runtime_log or \"reason=gi\" not in runtime_log or \"reason=integrity\" not in runtime_log:\n"
    "                raise RuntimeError(f\"P2 server report-send evidence missing: {runtime_log}\")\n"
    "            print(\n"
    "                \"IEDSIM_P2_REPORTING_PASS rptena=true gi=report integrity=report \"\n"
    "                \"information_report=true live_value=42 urcb_core=reused \"\n"
    "                \"IEDSIM_P1_UNIFIED_LIVE_VALUE_PASS \"\n",
)

print("P2 reporting patch applied")
