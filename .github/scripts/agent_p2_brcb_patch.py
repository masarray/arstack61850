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
        raise RuntimeError(f"{path}: expected one match, got {count}: {old[:180]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    text = read(path)
    updated, count = re.subn(pattern, lambda _: replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{path}: regex expected one match, got {count}: {pattern!r}")
    write(path, updated)


# ---------------------------------------------------------------------------
# Build the already-existing portable BRCB R1-R3 runtime into all host server
# profiles that compile the IED simulator server. The root server core already
# carries these sources; only the standalone Qt/parity profiles need wiring.
# ---------------------------------------------------------------------------
for cmake in ["apps/ied_simulator/CMakeLists.txt", "tools/static_ied_server_iedsim/CMakeLists.txt"]:
    if cmake == "apps/ied_simulator/CMakeLists.txt":
        old = (
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_report_connection.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_object_table.cpp\n"
        )
        new = (
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_report_connection.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/buffered_selective_information_report_span.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_runtime.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_control.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_objects.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_brcb_connection.cpp\n"
            "    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_object_table.cpp\n"
        )
    else:
        old = (
            "    \"${ARSTACK_ROOT}/src/mms/static_report_connection.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_object_table.cpp\"\n"
        )
        new = (
            "    \"${ARSTACK_ROOT}/src/mms/static_report_connection.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/buffered_selective_information_report_span.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_brcb_runtime.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_brcb_control.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_brcb_objects.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_brcb_connection.cpp\"\n"
            "    \"${ARSTACK_ROOT}/src/mms/static_object_table.cpp\"\n"
        )
    replace_once(cmake, old, new)

# ---------------------------------------------------------------------------
# SCL acceptance fixture: keep the existing URCB and add one buffered RCB bound
# to a writable SP simulator value. This gives P2 a deterministic live mutation
# source without opening P3 CO/control semantics.
# ---------------------------------------------------------------------------
fixture = "tests/fixtures/scl/iedsim-full-model.scd"
replace_once(
    fixture,
    "            <DataSet name=\"dsSV\">\n"
    "              <FCDA ldInst=\"LD0\" lnClass=\"TCTR\" lnInst=\"1\" doName=\"Amp\" daName=\"instMag.i\" fc=\"MX\" />\n"
    "            </DataSet>\n",
    "            <DataSet name=\"dsSV\">\n"
    "              <FCDA ldInst=\"LD0\" lnClass=\"TCTR\" lnInst=\"1\" doName=\"Amp\" daName=\"instMag.i\" fc=\"MX\" />\n"
    "            </DataSet>\n"
    "            <DataSet name=\"dsBuffered\">\n"
    "              <FCDA ldInst=\"LD0\" lnClass=\"XCBR\" lnInst=\"1\" doName=\"SimCfg\" daName=\"setVal\" fc=\"SP\" />\n"
    "            </DataSet>\n",
)
replace_once(
    fixture,
    "            </ReportControl>\n          </LN0>\n",
    "            </ReportControl>\n"
    "            <ReportControl name=\"brcb01\" datSet=\"dsBuffered\" rptID=\"MU01_LD0_BRCB01\" buffered=\"true\" bufTime=\"20\" intgPd=\"0\" confRev=\"8\" indexed=\"false\">\n"
    "              <TrgOps dchg=\"true\" />\n"
    "              <OptFields seqNum=\"true\" timeStamp=\"true\" reasonCode=\"true\" dataSet=\"true\" dataRef=\"true\" bufOvfl=\"true\" entryID=\"true\" configRef=\"true\" />\n"
    "            </ReportControl>\n"
    "          </LN0>\n",
)

# ---------------------------------------------------------------------------
# Host adapter: BRCB definitions and retained storage are pure adapters around
# MmsStaticBrcbRuntime/Control/ObjectBank/Connection. No queue, EntryID,
# reservation or replay semantics are reimplemented here.
# ---------------------------------------------------------------------------
server = "tools/static_ied_server.cpp"
replace_once(
    server,
    '#include "ariec61850/mms/static_report_connection.hpp"\n',
    '#include "ariec61850/mms/static_report_connection.hpp"\n'
    '#include "ariec61850/mms/static_brcb_connection.hpp"\n'
    '#include "ariec61850/mms/static_brcb_objects.hpp"\n'
    '#include "ariec61850/mms/static_brcb_runtime.hpp"\n',
)
replace_once(
    server,
    "    std::array<std::uint8_t, 32'768U> report_workspace{};\n};\n",
    "    std::array<std::uint8_t, 32'768U> report_workspace{};\n"
    "    std::array<std::uint8_t, 32'768U> brcb_capture_encode{};\n"
    "    std::array<std::uint8_t, 16'384U> brcb_capture_workspace{};\n"
    "};\n",
)

# Forward declaration lets the P1 mutation primitive feed the buffered-report
# runtime even though BRCB construction is defined after manifest materializing.
replace_once(
    server,
    "void emit_live_state(const ManifestValue& value, const std::uint64_t request_id) {\n",
    "void notify_active_brcb_mutation(\n"
    "    std::string_view domain, std::string_view item) noexcept;\n\n"
    "void emit_live_state(const ManifestValue& value, const std::uint64_t request_id) {\n",
)
replace_once(
    server,
    "    emit_live_state(value, request_id);\n    return true;\n}\n",
    "    emit_live_state(value, request_id);\n"
    "    notify_active_brcb_mutation(value.domain, value.item);\n"
    "    return true;\n"
    "}\n",
)

brcb_code = r'''
struct HostBrcbControl final {
    ManifestReportControl* manifest{};
    std::vector<mms::MmsStaticObjectEntry> member_objects;
    std::vector<std::string> member_keys;
    std::array<mms::MmsStaticDataSetEntry, 1U> data_set_entries{};
    mms::MmsStaticObjectTable object_table{std::span<const mms::MmsStaticObjectEntry>{}};
    mms::MmsStaticDataSetTable data_set_table{};
    mms::MmsStaticBrcbDefinition definition{};
    std::array<std::array<std::uint8_t, 16'384U>, 8U> slot_bytes{};
    std::array<mms::MmsStaticBrcbSlot, 8U> slots{};
    mms::MmsStaticBrcbPendingState pending{};
    std::unique_ptr<mms::MmsStaticBrcbRuntime> runtime;
    std::unique_ptr<mms::MmsStaticBrcbControl> control;
    std::vector<mms::MmsStaticObjectEntry> bank_objects;
    std::array<mms::MmsStaticBrcbObjectContext,
        mms::MmsStaticBrcbObjectBank::attributes_per_control_block> bank_contexts{};
    std::array<char, 4'096U> bank_names{};
    std::unique_ptr<mms::MmsStaticBrcbObjectBank> bank;
};

struct HostBrcbReporting final {
    std::vector<std::unique_ptr<HostBrcbControl>> controls;
};

HostBrcbReporting* g_active_brcb_reporting{};

[[nodiscard]] std::uint64_t brcb_now_ms(const void*) noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const auto count = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return count <= 0 ? 0U : static_cast<std::uint64_t>(count);
}

[[nodiscard]] HostBrcbReporting initialize_host_brcb_reporting(ManifestModel& model) {
    HostBrcbReporting reporting;
    for (auto& report : model.report_controls) {
        if (!report.buffered) continue;
        const auto data_set = std::find_if(
            model.data_sets.begin(), model.data_sets.end(), [&report](const auto& candidate) {
                return candidate.domain == report.data_set_domain &&
                    candidate.item == report.data_set_item;
            });
        if (data_set == model.data_sets.end() || data_set->members.empty()) {
            throw std::runtime_error(
                "BRCB references a missing/empty DataSet: " + report.domain + "/" + report.item);
        }

        auto host = std::make_unique<HostBrcbControl>();
        host->manifest = &report;
        host->member_objects.reserve(data_set->members.size());
        host->member_keys.reserve(data_set->members.size());
        for (const auto& member : data_set->members) {
            const auto object = std::find_if(
                model.objects.begin(), model.objects.end(), [&member](const auto& candidate) {
                    return candidate.domain == member.domain && candidate.item == member.item;
                });
            if (object == model.objects.end()) {
                throw std::runtime_error("BRCB DataSet member is absent from the live object table.");
            }
            host->member_objects.push_back(*object);
            host->member_keys.push_back(object_key(member.domain, member.item));
        }
        if (host->member_objects.empty() ||
            host->member_objects.size() > mms::MmsStaticObjectTable::maximum_objects) {
            throw std::runtime_error("BRCB DataSet exceeds the bounded static reporting object profile.");
        }
        host->object_table = mms::MmsStaticObjectTable{host->member_objects};
        host->data_set_entries[0] = *data_set;
        host->data_set_table = mms::MmsStaticDataSetTable{host->data_set_entries};
        if (!host->object_table.valid() || !host->data_set_table.valid() ||
            !host->data_set_table.valid_against(host->object_table)) {
            throw std::runtime_error("BRCB reporting subset failed strict static-table validation.");
        }

        if (report.report_id.empty()) report.report_id = report.domain + "/" + report.item;
        host->definition = mms::MmsStaticBrcbDefinition{
            report.domain,
            report.item,
            report.report_id,
            report.data_set_domain,
            report.data_set_item,
            report.conf_rev,
            report.optional_fields,
            report.buffer_time_ms,
            report.trigger_options};
        for (std::size_t index = 0U; index < host->slots.size(); ++index) {
            host->slots[index].storage = host->slot_bytes[index];
        }
        host->runtime = std::make_unique<mms::MmsStaticBrcbRuntime>(
            host->definition,
            host->pending,
            host->slots,
            host->object_table,
            host->data_set_table);
        if (!host->runtime->initialize()) {
            throw std::runtime_error(
                "MmsStaticBrcbRuntime rejected SCL report definition: " +
                report.domain + "/" + report.item);
        }
        host->control = std::make_unique<mms::MmsStaticBrcbControl>(*host->runtime);
        host->bank_objects.resize(
            host->member_objects.size() + mms::MmsStaticBrcbObjectBank::attributes_per_control_block);
        host->bank = std::make_unique<mms::MmsStaticBrcbObjectBank>(
            host->definition,
            *host->runtime,
            *host->control,
            host->member_objects,
            host->bank_objects,
            host->bank_contexts,
            host->bank_names,
            brcb_now_ms,
            nullptr);
        if (!host->bank->initialize()) {
            throw std::runtime_error("MmsStaticBrcbObjectBank failed to materialize dynamic BRCB attributes.");
        }

        const auto attribute_prefix = report.item + "$";
        for (const auto& dynamic : host->bank->table().objects()) {
            if (!dynamic.item.starts_with(attribute_prefix)) continue;
            const auto existing = std::find_if(
                model.objects.begin(), model.objects.end(), [&dynamic](const auto& candidate) {
                    return candidate.domain == dynamic.domain && candidate.item == dynamic.item;
                });
            if (existing == model.objects.end()) {
                if (model.objects.size() >= kHostMaximumManifestObjects) {
                    throw std::runtime_error("Dynamic BRCB attributes exceed host object bound.");
                }
                model.objects.push_back(dynamic);
            } else {
                *existing = dynamic;
            }
        }
        reporting.controls.push_back(std::move(host));
    }
    return reporting;
}

void notify_active_brcb_mutation(
    const std::string_view domain,
    const std::string_view item) noexcept {
    if (g_active_brcb_reporting == nullptr) return;
    const auto key = object_key(domain, item);
    const auto now = brcb_now_ms(nullptr);
    for (auto& host : g_active_brcb_reporting->controls) {
        for (std::size_t index = 0U; index < host->member_keys.size(); ++index) {
            if (host->member_keys[index] != key) continue;
            const auto status = host->runtime->notify(
                index, mms::MmsStaticBrcbEventReason::data_change, now);
            if (status == mms::MmsStaticBrcbStatus::ok) {
                std::cout << "IEDSIM_EVENT kind=brcb_event rcb="
                          << host->manifest->domain << '/' << host->manifest->item
                          << " member=" << domain << '/' << item << '\n';
                std::cout.flush();
            }
        }
    }
}

void host_brcb_association_closed(
    void* context,
    const std::uint64_t association_id,
    const std::uint64_t now_ms) noexcept {
    auto* reporting = static_cast<HostBrcbReporting*>(context);
    if (reporting == nullptr) return;
    for (auto& host : reporting->controls) {
        host->control->on_association_closed(association_id, now_ms);
    }
}

[[nodiscard]] bool capture_host_brcb_reports(
    HostBrcbReporting& reporting,
    ConnectionBuffers& buffers) {
    const auto now = brcb_now_ms(nullptr);
    const auto report_time = report_binary_time();
    for (auto& host : reporting.controls) {
        host->control->tick(now);
        mms::MmsStaticBrcbCapturePlan plan;
        if (!host->runtime->next_due(now, plan)) continue;
        const auto captured = host->runtime->capture(
            plan,
            report_time,
            buffers.brcb_capture_encode,
            buffers.brcb_capture_workspace);
        if (!captured.success()) {
            std::cerr << "IEDSIM_EVENT kind=brcb_capture_error rcb="
                      << host->manifest->domain << '/' << host->manifest->item
                      << " status=" << static_cast<unsigned>(captured.status) << '\n';
            return false;
        }
        std::cout << "IEDSIM_EVENT kind=brcb_captured rcb="
                  << host->manifest->domain << '/' << host->manifest->item
                  << " sequence=" << static_cast<unsigned>(plan.sequence_number)
                  << " retained=" << host->runtime->retained_size()
                  << " queue=" << host->runtime->queue_size() << '\n';
        std::cout.flush();
    }
    return true;
}

[[nodiscard]] bool poll_host_brcb_reports(
    HostBrcbReporting& reporting,
    const mms::MmsStaticConnectionRuntime& connection,
    const embedded::TcpByteStream& stream,
    ConnectionBuffers& buffers,
    const std::uint64_t association_id,
    const std::string_view remote,
    std::size_t& total_sent) {
    const auto now = brcb_now_ms(nullptr);
    for (auto& host : reporting.controls) {
        const auto staged = mms::MmsStaticBrcbConnection::poll(
            connection,
            *host->control,
            *host->runtime,
            now,
            buffers.report_response,
            buffers.report_workspace);
        if (staged.status == mms::MmsStaticBrcbConnectionStatus::no_report_available ||
            staged.status == mms::MmsStaticBrcbConnectionStatus::reporting_disabled ||
            staged.status == mms::MmsStaticBrcbConnectionStatus::not_established ||
            staged.status == mms::MmsStaticBrcbConnectionStatus::access_denied) {
            continue;
        }
        if (!staged.response_ready()) {
            std::cerr << "IEDSIM_EVENT kind=brcb_stage_error association=" << association_id
                      << " rcb=" << host->manifest->domain << '/' << host->manifest->item
                      << " status=" << static_cast<unsigned>(staged.status) << '\n';
            return false;
        }
        if (!send_complete_report_frame(
                stream,
                std::span<const std::uint8_t>{buffers.report_response.data(), staged.bytes_written},
                total_sent)) {
            std::cerr << "IEDSIM_EVENT kind=brcb_send_error association=" << association_id
                      << " rcb=" << host->manifest->domain << '/' << host->manifest->item
                      << " remote=" << remote << '\n';
            return false;
        }
        if (!mms::MmsStaticBrcbConnection::commit_sent(
                connection, *host->control, *host->runtime, now, staged)) {
            std::cerr << "IEDSIM_EVENT kind=brcb_commit_error association=" << association_id
                      << " rcb=" << host->manifest->domain << '/' << host->manifest->item << '\n';
            return false;
        }
        std::cout << "IEDSIM_EVENT kind=brcb_report_sent association=" << association_id
                  << " remote=" << remote
                  << " rcb=" << host->manifest->domain << '/' << host->manifest->item
                  << " sequence=" << static_cast<unsigned>(staged.sequence_number)
                  << " bytes=" << staged.bytes_written
                  << " retained=" << host->runtime->retained_size()
                  << " queue=" << host->runtime->queue_size() << '\n';
        std::cout.flush();
        return true;
    }
    return true;
}

'''
replace_once(
    server,
    "[[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {\n",
    brcb_code + "[[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {\n",
)

# Add the BRCB adapter to the established session. Association close is fanned
# into the core control state so ResvTms/Owner lifecycle stays inside R1-R3.
replace_once(
    server,
    "    HostUrcbReporting* const reporting,\n"
    "    const std::uint64_t association_id,\n",
    "    HostUrcbReporting* const reporting,\n"
    "    HostBrcbReporting* const brcb_reporting,\n"
    "    const std::uint64_t association_id,\n",
)
replace_once(
    server,
    "    for (std::size_t index = 0U; index < policy.owner_size; ++index) {\n"
    "        const auto shift = static_cast<unsigned>((policy.owner_size - 1U - index) * 8U);\n"
    "        policy.owner[index] = static_cast<std::uint8_t>(\n"
    "            (association_id >> shift) & 0xFFU);\n"
    "    }\n\n"
    "    mms::MmsStaticConnectionRuntime runtime{dispatcher, policy};\n",
    "    for (std::size_t index = 0U; index < policy.owner_size; ++index) {\n"
    "        const auto shift = static_cast<unsigned>((policy.owner_size - 1U - index) * 8U);\n"
    "        policy.owner[index] = static_cast<std::uint8_t>(\n"
    "            (association_id >> shift) & 0xFFU);\n"
    "    }\n"
    "    if (brcb_reporting != nullptr) {\n"
    "        policy.now_ms = brcb_now_ms;\n"
    "        policy.association_closed = host_brcb_association_closed;\n"
    "        policy.association_closed_context = brcb_reporting;\n"
    "    }\n\n"
    "    mms::MmsStaticConnectionRuntime runtime{dispatcher, policy};\n",
)

# The URCB patch inserted its poll immediately before terminal handling. Capture
# and deliver retained BRCB entries at the same safe point: confirmed output and
# buffered input must both be drained before unsolicited report delivery.
replace_once(
    server,
    "        if (reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&\n"
    "            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&\n"
    "            !poll_host_urcb_reports(\n"
    "                *reporting, runtime, stream, buffers, association_id, remote, total_sent)) {\n"
    "            reset_host_urcb_connection(reporting);\n"
    "            runtime.close_transport();\n"
    "            return;\n"
    "        }\n"
    "        if (result.terminal()) {\n",
    "        if (brcb_reporting != nullptr && !capture_host_brcb_reports(*brcb_reporting, buffers)) {\n"
    "            reset_host_urcb_connection(reporting);\n"
    "            runtime.close_transport();\n"
    "            return;\n"
    "        }\n"
    "        if (reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&\n"
    "            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&\n"
    "            !poll_host_urcb_reports(\n"
    "                *reporting, runtime, stream, buffers, association_id, remote, total_sent)) {\n"
    "            reset_host_urcb_connection(reporting);\n"
    "            runtime.close_transport();\n"
    "            return;\n"
    "        }\n"
    "        if (brcb_reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&\n"
    "            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&\n"
    "            !poll_host_brcb_reports(\n"
    "                *brcb_reporting, runtime, stream, buffers, association_id, remote, total_sent)) {\n"
    "            reset_host_urcb_connection(reporting);\n"
    "            runtime.close_transport();\n"
    "            return;\n"
    "        }\n"
    "        if (result.terminal()) {\n",
)

# Initialize both report families before constructing the final immutable host
# object table. Then expose counts separately so acceptance can prove which core
# is active.
replace_once(
    server,
    "        auto urcb_reporting = initialize_host_urcb_reporting(manifest_model);\n\n"
    "        std::array<mms::MmsStaticObjectEntry, 13U> objects{};\n",
    "        auto urcb_reporting = initialize_host_urcb_reporting(manifest_model);\n"
    "        auto brcb_reporting = initialize_host_brcb_reporting(manifest_model);\n"
    "        g_active_brcb_reporting = brcb_reporting.controls.empty() ? nullptr : &brcb_reporting;\n\n"
    "        std::array<mms::MmsStaticObjectEntry, 13U> objects{};\n",
)
replace_once(
    server,
    "            std::cout << \"IEDSIM_EVENT kind=reporting_ready urcb=\"\n"
    "                      << urcb_reporting.controls.size()\n"
    "                      << \" runtime=static-urcb-core\" << '\\n';\n",
    "            std::cout << \"IEDSIM_EVENT kind=reporting_ready urcb=\"\n"
    "                      << urcb_reporting.controls.size()\n"
    "                      << \" brcb=\" << brcb_reporting.controls.size()\n"
    "                      << \" runtime=static-urcb+brcb-core\" << '\\n';\n",
)
replace_once(
    server,
    "                urcb_reporting.controls.empty() ? nullptr : &urcb_reporting,\n"
    "                static_cast<std::uint64_t>(connection_count),\n",
    "                urcb_reporting.controls.empty() ? nullptr : &urcb_reporting,\n"
    "                brcb_reporting.controls.empty() ? nullptr : &brcb_reporting,\n"
    "                static_cast<std::uint64_t>(connection_count),\n",
)
replace_once(
    server,
    "        close_socket(listener);\n"
    "        std::cout << \"IEDSIM_EVENT kind=server_stopped connections=\"\n",
    "        close_socket(listener);\n"
    "        g_active_brcb_reporting = nullptr;\n"
    "        std::cout << \"IEDSIM_EVENT kind=server_stopped connections=\"\n",
)

# ---------------------------------------------------------------------------
# Extend the existing static RCB client with one deliberately bounded Boolean
# MMS Write performed on the SAME association after RptEna. This is not a new
# reporting mechanism: it simply exercises P1's existing writable SP mutation
# path so the BRCB can observe and buffer a genuine live-state change.
# ---------------------------------------------------------------------------
trial = "tools/static_rcb_trial.cpp"
replace_once(
    trial,
    "    bool trigger_gi{true};\n    bool armed{};\n",
    "    bool trigger_gi{true};\n"
    "    bool armed{};\n"
    "    bool exercise_write_bool{};\n"
    "    bool exercise_write_value{};\n"
    "    std::string exercise_write_domain;\n"
    "    std::string exercise_write_item;\n",
)
replace_once(
    trial,
    '        << "  --no-gi                  Do not request GI after enabling.\\n"\n',
    '        << "  --no-gi                  Do not request GI after enabling.\\n"\n'
    '        << "  --exercise-write-bool DOMAIN/ITEM=VALUE\\n"\n'
    '        << "                            Same-association bounded Boolean write after enable.\\n"\n',
)
replace_once(
    trial,
    "                   option == \"--probe-delay-ms\" ||\n"
    "                   option == \"--timeout-ms\" || option == \"--arm\") {\n",
    "                   option == \"--probe-delay-ms\" ||\n"
    "                   option == \"--timeout-ms\" ||\n"
    "                   option == \"--exercise-write-bool\" || option == \"--arm\") {\n",
)
replace_once(
    trial,
    "            } else if (option == \"--timeout-ms\") {\n"
    "                options.timeout = std::chrono::milliseconds{\n"
    "                    static_cast<std::int64_t>(parse_positive(option, value))};\n"
    "            } else if (option == \"--arm\") {\n",
    "            } else if (option == \"--timeout-ms\") {\n"
    "                options.timeout = std::chrono::milliseconds{\n"
    "                    static_cast<std::int64_t>(parse_positive(option, value))};\n"
    "            } else if (option == \"--exercise-write-bool\") {\n"
    "                const auto equals = value.rfind('=');\n"
    "                const auto slash = value.find('/');\n"
    "                if (equals == std::string::npos || slash == std::string::npos ||\n"
    "                    slash == 0U || slash >= equals || equals + 1U >= value.size()) {\n"
    "                    throw std::invalid_argument(\"--exercise-write-bool expects DOMAIN/ITEM=true|false.\");\n"
    "                }\n"
    "                options.exercise_write_domain = value.substr(0U, slash);\n"
    "                options.exercise_write_item = value.substr(slash + 1U, equals - slash - 1U);\n"
    "                const auto boolean = value.substr(equals + 1U);\n"
    "                if (boolean == \"true\" || boolean == \"1\") options.exercise_write_value = true;\n"
    "                else if (boolean == \"false\" || boolean == \"0\") options.exercise_write_value = false;\n"
    "                else throw std::invalid_argument(\"--exercise-write-bool expects true/false.\");\n"
    "                options.exercise_write_bool = true;\n"
    "            } else if (option == \"--arm\") {\n",
)

exercise_fn = r'''
void exercise_same_association_boolean_write(
    mms::MmsAssociationRuntime& association,
    const CliOptions& cli) {
    if (!cli.exercise_write_bool) return;
    const auto invoke_id = association.next_invoke_id();
    mms::MmsWriteRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(mms::MmsObjectName::domain_specific(
        cli.exercise_write_domain, cli.exercise_write_item));
    request.values.push_back(mms::MmsDataValue::boolean(cli.exercise_write_value));
    const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("BRCB exercise Write did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(
        response_payload(exchange), invoke_id);
    if (!response.all_success()) {
        throw std::runtime_error("BRCB exercise Write returned a failed AccessResult.");
    }
    std::cout << "EXERCISE_MMS_WRITE reference="
              << cli.exercise_write_domain << '/' << cli.exercise_write_item
              << " value=" << (cli.exercise_write_value ? "true" : "false") << '\n';
    std::cout.flush();
}

'''
replace_once(
    trial,
    "void confirmation_read(\n",
    exercise_fn + "void confirmation_read(\n",
)
replace_once(
    trial,
    "            std::cout << \"STATIC_RCB_ENABLE_OK gi=\"\n"
    "                      << (cli.trigger_gi ? \"requested\" : \"not-requested\")\n"
    "                      << '\\n';\n"
    "            for (std::size_t cycle = 0U;\n",
    "            std::cout << \"STATIC_RCB_ENABLE_OK gi=\"\n"
    "                      << (cli.trigger_gi ? \"requested\" : \"not-requested\")\n"
    "                      << '\\n';\n"
    "            exercise_same_association_boolean_write(\n"
    "                live_session.association(), cli);\n"
    "            for (std::size_t cycle = 0U;\n",
)

# ---------------------------------------------------------------------------
# End-to-end acceptance: run URCB and BRCB independently. URCB proves GI +
# integrity + live value 42. BRCB proves a same-association P1 MMS mutation is
# retained, encoded, sent, committed, decoded and cleaned up through R1-R3.
# ---------------------------------------------------------------------------
test = "apps/ied_simulator/test_gui_live_value.py"
replace_once(test, '                "--undo-after-ms",\n                "10500",\n', '                "--undo-after-ms",\n                "20000",\n')
replace_once(test, '                "--exit-after-ms",\n                "15000",\n', '                "--exit-after-ms",\n                "28000",\n')
replace_once(test, "            app.wait(timeout=19)\n", "            app.wait(timeout=32)\n")
replace_once(
    test,
    "                data_set_present = (\n"
    "                    \"DS\\tMU01LD0\\tLLN0$dsSV\\tMU01LD0\\tTCTR1$MX$Amp$instMag$i\"\n"
    "                    in manifest_text\n"
    "                )\n",
    "                data_set_present = (\n"
    "                    \"DS\\tMU01LD0\\tLLN0$dsSV\\tMU01LD0\\tTCTR1$MX$Amp$instMag$i\"\n"
    "                    in manifest_text\n"
    "                )\n"
    "                buffered_data_set_present = (\n"
    "                    \"DS\\tMU01LD0\\tLLN0$dsBuffered\\tMU01LD0\\tXCBR1$SP$SimCfg$setVal\"\n"
    "                    in manifest_text\n"
    "                )\n",
)
replace_once(
    test,
    "                rcb_present = (\n"
    "                    \"RCB\\tMU01LD0\\tLLN0$RP$urcb01\\t0\\tMU01_LD0_URCB01\\t\"\n"
    "                    \"MU01LD0\\tLLN0$dsSV\\t7\\t20\\t1000\\t0\" in manifest_text\n"
    "                )\n",
    "                rcb_present = (\n"
    "                    \"RCB\\tMU01LD0\\tLLN0$RP$urcb01\\t0\\tMU01_LD0_URCB01\\t\"\n"
    "                    \"MU01LD0\\tLLN0$dsSV\\t7\\t20\\t1000\\t0\" in manifest_text\n"
    "                )\n"
    "                brcb_present = (\n"
    "                    \"RCB\\tMU01LD0\\tLLN0$BR$brcb01\\t1\\tMU01_LD0_BRCB01\\t\"\n"
    "                    \"MU01LD0\\tLLN0$dsBuffered\\t8\\t20\\t0\\t0\" in manifest_text\n"
    "                )\n",
)
replace_once(
    test,
    "                    and data_set_present\n                    and rcb_present\n",
    "                    and data_set_present\n"
    "                    and buffered_data_set_present\n"
    "                    and rcb_present\n"
    "                    and brcb_present\n",
)

# With both RCB families present, explicitly select the IEC-facing URCB name.
replace_once(
    test,
    "                    str(port),\n"
    "                    \"--probe-cycles\",\n"
    "                    \"6\",\n",
    "                    str(port),\n"
    "                    \"--preferred-rcb\",\n"
    "                    \"MU01LD0/LLN0.urcb01\",\n"
    "                    \"--probe-cycles\",\n"
    "                    \"6\",\n",
)

brcb_trial = r'''
            brcb_trial = subprocess.run(
                [
                    report_probe,
                    "127.0.0.1",
                    str(port),
                    "--preferred-rcb",
                    "MU01LD0/LLN0.brcb01",
                    "--no-urcb-fallback",
                    "--no-gi",
                    "--exercise-write-bool",
                    "MU01LD0/XCBR1$SP$SimCfg$setVal=false",
                    "--probe-cycles",
                    "5",
                    "--probe-delay-ms",
                    "200",
                    "--timeout-ms",
                    "3000",
                    "--arm",
                    "IEC61850-LAB-STATIC-RCB",
                ],
                capture_output=True,
                text=True,
                timeout=12,
                check=False,
                creationflags=creation_flags(),
            )
            if brcb_trial.returncode != 0 or "SMART_STATIC_RCB_TRIAL_PASS" not in brcb_trial.stdout:
                raise RuntimeError(
                    "P2 BRCB R1-R3 trial failed: "
                    f"exit={brcb_trial.returncode} stdout={brcb_trial.stdout} stderr={brcb_trial.stderr}"
                )
            if "STATIC_PLAN selectedRcb=MU01LD0/LLN0.brcb01 mode=BRCB dataSet=MU01LD0/LLN0$dsBuffered" not in brcb_trial.stdout:
                raise RuntimeError(f"P2 BRCB/DataSet binding mismatch: {brcb_trial.stdout}")
            if "EXERCISE_MMS_WRITE reference=MU01LD0/XCBR1$SP$SimCfg$setVal value=false" not in brcb_trial.stdout:
                raise RuntimeError(f"P2 BRCB live mutation was not executed: {brcb_trial.stdout}")
            brcb_evidence = re.search(r"REPORT_EVIDENCE received=(\d+) decodeFailures=(\d+)", brcb_trial.stdout)
            if brcb_evidence is None or int(brcb_evidence.group(1)) < 1 or int(brcb_evidence.group(2)) != 0:
                raise RuntimeError(f"P2 BRCB InformationReport evidence missing: {brcb_trial.stdout}")
            if "REPORT_VALUE" not in brcb_trial.stdout or "value=false" not in brcb_trial.stdout:
                raise RuntimeError(f"P2 BRCB report did not carry authoritative live value false: {brcb_trial.stdout}")

'''
replace_once(
    test,
    "            # MX is deliberately not generic-write enabled. CO/control remains a P3\n",
    brcb_trial + "            # MX is deliberately not generic-write enabled. CO/control remains a P3\n",
)

# BRCB exercise is the third successful live mutation. TCTR undo remains guarded
# by its per-value revision but advances the global deterministic clock to 4.
replace_once(
    test,
    '                "timestamp": "1970-01-01T00:00:00.003Z",\n                "liveRevision": 3,\n',
    '                "timestamp": "1970-01-01T00:00:00.004Z",\n                "liveRevision": 4,\n',
)
replace_once(
    test,
    "            expected_simcfg = {\n"
    "                \"value\": \"true\",\n"
    "                \"quality\": \"Good\",\n"
    "                \"origin\": \"mms-write\",\n"
    "                \"timestamp\": \"1970-01-01T00:00:00.002Z\",\n"
    "                \"liveRevision\": 2,\n"
    "            }\n",
    "            expected_simcfg = {\n"
    "                \"value\": \"false\",\n"
    "                \"quality\": \"Good\",\n"
    "                \"origin\": \"mms-write\",\n"
    "                \"timestamp\": \"1970-01-01T00:00:00.003Z\",\n"
    "                \"liveRevision\": 3,\n"
    "            }\n",
)
replace_once(
    test,
    "            if \"kind=report_sent\" not in runtime_log or \"reason=gi\" not in runtime_log or \"reason=integrity\" not in runtime_log:\n"
    "                raise RuntimeError(f\"P2 server report-send evidence missing: {runtime_log}\")\n",
    "            if \"kind=report_sent\" not in runtime_log or \"reason=gi\" not in runtime_log or \"reason=integrity\" not in runtime_log:\n"
    "                raise RuntimeError(f\"P2 URCB server report-send evidence missing: {runtime_log}\")\n"
    "            if \"kind=brcb_event\" not in runtime_log or \"kind=brcb_captured\" not in runtime_log or \"kind=brcb_report_sent\" not in runtime_log:\n"
    "                raise RuntimeError(f\"P2 BRCB retained-delivery evidence missing: {runtime_log}\")\n",
)
replace_once(
    test,
    "                \"information_report=true live_value=42 urcb_core=reused \"\n"
    "                \"IEDSIM_P1_UNIFIED_LIVE_VALUE_PASS \"\n"
    "                \"gui_to_mms=42 mms_to_qt=SP:true undo=0 \"\n"
    "                \"clock_ms=1,2,3 origin=Simulator-QA,mms-write,gui-undo \"\n",
    "                \"information_report=true live_value=42 urcb_core=reused \"\n"
    "                \"brcb_r1_r3=true retained_capture=true two_phase_commit=true brcb_live_value=false \"\n"
    "                \"IEDSIM_P1_UNIFIED_LIVE_VALUE_PASS \"\n"
    "                \"gui_to_mms=42 mms_to_qt=SP:true undo=0 \"\n"
    "                \"clock_ms=1,2,3,4 origin=Simulator-QA,mms-write,mms-write,gui-undo \"\n",
)

print("P2 BRCB R1-R3 integration patch applied")
