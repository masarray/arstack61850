from pathlib import Path


def replace(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count} occurrence(s), found {actual}")
    p.write_text(text.replace(old, new, count), encoding="utf-8")


def write(path: str, content: str) -> None:
    p = Path(path)
    if p.exists():
        raise SystemExit(f"{path}: already exists")
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")


# 1) Testable SCL -> runtime-manifest projection. Invalid SettingControl is
# deliberately omitted; parser warnings already carry the source defect.
write(
    "apps/ied_simulator/src/IedSettingControlManifest.hpp",
    r'''// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/scl/model.hpp"

#include <QByteArray>
#include <QLatin1Char>
#include <QSet>
#include <QString>

namespace arstack::iedsim {

inline QByteArray settingManifestField(QString value) {
    value.replace(QLatin1Char('\t'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value.toUtf8();
}

inline QByteArray settingControlManifestLine(
    const ar::iec61850::scl::SclSettingControl& setting,
    const QString& activeIedName,
    QSet<QString>& emittedSettingControls) {
    const auto iedName = QString::fromStdString(setting.ied_name);
    if (iedName != activeIedName || !setting.valid()) return {};

    const auto domain = iedName + QString::fromStdString(setting.ld_inst);
    auto logicalNode = QString::fromStdString(setting.logical_node_path);
    logicalNode.replace(QLatin1Char('.'), QLatin1Char('$'));
    const auto item = logicalNode + QStringLiteral("$SP$SGCB");
    if (domain.isEmpty() || logicalNode.isEmpty()) return {};

    const auto key = domain + QLatin1Char('\n') + item;
    if (emittedSettingControls.contains(key)) return {};
    emittedSettingControls.insert(key);

    return "SGCB\t" + settingManifestField(domain) + "\t" +
        settingManifestField(item) + "\t" +
        QByteArray::number(*setting.number_of_setting_groups) + "\t" +
        QByteArray::number(*setting.active_setting_group) + "\n";
}

} // namespace arstack::iedsim
''')

write(
    "apps/ied_simulator/src/ied_setting_control_manifest_qa.cpp",
    r'''// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedSettingControlManifest.hpp"

#include <QCoreApplication>
#include <QSet>

#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) throw std::runtime_error(std::string{"CHECK failed: "} + #condition); \
} while (false)

int main(int argc, char** argv) {
    QCoreApplication application{argc, argv};
    try {
        using ar::iec61850::scl::SclSettingControl;
        SclSettingControl valid;
        valid.ied_name = "SGIED";
        valid.ld_inst = "LD0";
        valid.logical_node_path = "LLN0";
        valid.control_block_reference = "SGIEDLD0/LLN0$SP$SGCB";
        valid.number_of_setting_groups = 4U;
        valid.active_setting_group = 1U;

        QSet<QString> emitted;
        const auto line = arstack::iedsim::settingControlManifestLine(
            valid, QStringLiteral("SGIED"), emitted);
        CHECK(line == QByteArray{"SGCB\tSGIEDLD0\tLLN0$SP$SGCB\t4\t1\n"});
        CHECK(arstack::iedsim::settingControlManifestLine(
            valid, QStringLiteral("SGIED"), emitted).isEmpty());

        QSet<QString> wrongIed;
        CHECK(arstack::iedsim::settingControlManifestLine(
            valid, QStringLiteral("OTHER"), wrongIed).isEmpty());

        auto invalid = valid;
        invalid.active_setting_group = 5U;
        QSet<QString> invalidSet;
        CHECK(arstack::iedsim::settingControlManifestLine(
            invalid, QStringLiteral("SGIED"), invalidSet).isEmpty());

        std::cout << "SETTING_CONTROL_MANIFEST_PASS exact=pass duplicate=pass invalid=omitted\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SETTING_CONTROL_MANIFEST_FAIL " << error.what() << '\n';
        return 1;
    }
}
''')

replace(
    "apps/ied_simulator/src/IedFleetController.cpp",
    '#include "IedReportControlManifest.hpp"\n',
    '#include "IedReportControlManifest.hpp"\n#include "IedSettingControlManifest.hpp"\n')
replace(
    "apps/ied_simulator/src/IedFleetController.cpp",
    '''    QSet<QString> emittedReportControls;\n    for (const auto& report : loaded.document.report_controls) {\n        manifest += arstack::iedsim::reportControlManifestLines(\n            report,\n            activeIedName,\n            emittedReportControls);\n    }\n\n''',
    '''    QSet<QString> emittedReportControls;\n    for (const auto& report : loaded.document.report_controls) {\n        manifest += arstack::iedsim::reportControlManifestLines(\n            report,\n            activeIedName,\n            emittedReportControls);\n    }\n\n    QSet<QString> emittedSettingControls;\n    for (const auto& setting : loaded.document.setting_controls) {\n        manifest += arstack::iedsim::settingControlManifestLine(\n            setting, activeIedName, emittedSettingControls);\n    }\n\n''')

# 2) Portable field probe: proves NameList discovery, exact five attributes,
# guarded ActSG Write, verification Read, and expected rejection.
write(
    "tools/mms_sgcb_probe.cpp",
    r'''// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
namespace mms = ar::iec61850::mms;

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const mms::MmsConfirmedExchangeResult& exchange) {
    return exchange.presentation_payload.empty()
        ? exchange.envelope.mms_payload
        : std::span<const std::uint8_t>{exchange.presentation_payload};
}

[[nodiscard]] std::uint64_t parse_u64(
    const std::string& option,
    const std::string& text,
    const std::uint64_t maximum) {
    std::size_t consumed{};
    const auto value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value > maximum) {
        throw std::invalid_argument(option + " is outside the supported range.");
    }
    return value;
}

[[nodiscard]] mms::MmsDataValue read_one(
    mms::MmsAssociationRuntime& association,
    const std::string& domain,
    const std::string& item) {
    mms::MmsReadRequest request;
    request.invoke_id = association.next_invoke_id();
    request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));
    const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, request.invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("SGCB Read did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_read_response(
        response_payload(exchange), request.invoke_id);
    if (response.results.size() != 1U || !response.results.front().success()) {
        throw std::runtime_error("SGCB Read returned failed AccessResult for " + item + '.');
    }
    return *response.results.front().value;
}

[[nodiscard]] std::uint64_t as_unsigned(
    const mms::MmsDataValue& value,
    const std::string_view label) {
    if (value.kind() == mms::MmsDataKind::unsigned_integer) {
        return std::get<std::uint64_t>(value.value());
    }
    if (value.kind() == mms::MmsDataKind::integer) {
        const auto parsed = std::get<std::int64_t>(value.value());
        if (parsed >= 0) return static_cast<std::uint64_t>(parsed);
    }
    throw std::runtime_error(std::string{label} + " is not an unsigned/integer scalar.");
}

[[nodiscard]] bool as_bool(const mms::MmsDataValue& value, const std::string_view label) {
    if (value.kind() != mms::MmsDataKind::boolean) {
        throw std::runtime_error(std::string{label} + " is not Boolean.");
    }
    return std::get<bool>(value.value());
}

[[nodiscard]] bool write_actsg(
    mms::MmsAssociationRuntime& association,
    const std::string& domain,
    const std::string& item,
    const std::uint64_t group) {
    mms::MmsWriteRequest request;
    request.invoke_id = association.next_invoke_id();
    request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));
    request.values.push_back(mms::MmsDataValue::unsigned_integer(group));
    const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, request.invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("ActSG Write did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(
        response_payload(exchange), request.invoke_id);
    if (response.results.size() != 1U) {
        throw std::runtime_error("ActSG Write response cardinality mismatch.");
    }
    return response.all_success();
}

void usage() {
    std::cout
        << "Usage: ariec61850_mms_sgcb_probe <host> [port] --domain D --root ITEM [options]\n"
        << "  --expect-num N   Require NumOfSG.\n"
        << "  --expect-act N   Require initial ActSG.\n"
        << "  --activate N     Write ActSG=N and verify it.\n"
        << "  --reject N       Require ActSG=N to be rejected and state unchanged.\n"
        << "  --timeout-ms N   Connect/request timeout, default 5000.\n";
}
} // namespace

int main(const int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view{argv[1]} == "--help" ||
            std::string_view{argv[1]} == "-h") {
            usage();
            return argc < 2 ? 2 : 0;
        }
        mms::MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = 102U;
        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.rfind("--", 0U) != 0U) {
            endpoint.port = static_cast<std::uint16_t>(
                parse_u64("port", argv[argument++], 65'535U));
        }
        std::string domain;
        std::string root;
        std::optional<std::uint64_t> expected_num;
        std::optional<std::uint64_t> expected_act;
        std::optional<std::uint64_t> activate;
        std::optional<std::uint64_t> reject;
        std::chrono::milliseconds timeout{5'000};
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
            const std::string value = argv[argument++];
            if (option == "--domain") domain = value;
            else if (option == "--root") root = value;
            else if (option == "--expect-num") expected_num = parse_u64(option, value, 255U);
            else if (option == "--expect-act") expected_act = parse_u64(option, value, 255U);
            else if (option == "--activate") activate = parse_u64(option, value, 255U);
            else if (option == "--reject") reject = parse_u64(option, value, 255U);
            else if (option == "--timeout-ms") {
                timeout = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parse_u64(option, value, 120'000U))};
            } else throw std::invalid_argument("Unknown option: " + option);
        }
        if (domain.empty() || root.empty()) {
            throw std::invalid_argument("--domain and --root are required.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);

        mms::MmsLiveDiscoveryOptions discovery_options;
        discovery_options.probe_variable_types = false;
        discovery_options.read_data_set_directories = false;
        discovery_options.probe_report_controls = false;
        const auto discovered = session.discover(discovery_options);
        const auto found_domain = discovered.names.domain_variables.find(domain);
        if (found_domain == discovered.names.domain_variables.end()) {
            throw std::runtime_error("SGCB domain missing from GetNameList discovery.");
        }
        const std::vector<std::string> suffixes{
            "ActSG", "CnfEdit", "EditSG", "LActTm", "NumOfSG"};
        for (const auto& suffix : suffixes) {
            const auto exact = root + '$' + suffix;
            if (std::find(found_domain->second.begin(), found_domain->second.end(), exact) ==
                found_domain->second.end()) {
                throw std::runtime_error("GetNameList missing exact SGCB attribute: " + exact);
            }
        }

        const auto act_item = root + "$ActSG";
        const auto act = as_unsigned(read_one(session.association(), domain, act_item), "ActSG");
        const auto count = as_unsigned(
            read_one(session.association(), domain, root + "$NumOfSG"), "NumOfSG");
        const auto edit = as_unsigned(
            read_one(session.association(), domain, root + "$EditSG"), "EditSG");
        const auto confirm = as_bool(
            read_one(session.association(), domain, root + "$CnfEdit"), "CnfEdit");
        const auto last = read_one(session.association(), domain, root + "$LActTm");
        if (last.kind() != mms::MmsDataKind::utc_time) {
            throw std::runtime_error("LActTm is not IEC 61850 UTC time.");
        }
        if (edit != 0U || confirm) {
            throw std::runtime_error("Simulator SGCB unexpectedly advertises active edit transaction.");
        }
        if (expected_num && count != *expected_num) {
            throw std::runtime_error("NumOfSG verification mismatch.");
        }
        if (expected_act && act != *expected_act) {
            throw std::runtime_error("Initial ActSG verification mismatch.");
        }

        std::uint64_t final_act = act;
        if (activate) {
            if (!write_actsg(session.association(), domain, act_item, *activate)) {
                throw std::runtime_error("Expected ActSG activation was rejected.");
            }
            final_act = as_unsigned(
                read_one(session.association(), domain, act_item), "ActSG verification");
            if (final_act != *activate) {
                throw std::runtime_error("ActSG verification Read did not observe requested group.");
            }
        }
        if (reject) {
            const auto before = as_unsigned(
                read_one(session.association(), domain, act_item), "ActSG before reject");
            if (write_actsg(session.association(), domain, act_item, *reject)) {
                throw std::runtime_error("Expected invalid ActSG Write to be rejected.");
            }
            final_act = as_unsigned(
                read_one(session.association(), domain, act_item), "ActSG after reject");
            if (final_act != before) {
                throw std::runtime_error("Rejected ActSG Write changed canonical state.");
            }
        }

        std::cout << "SGCB_PROBE_PASS discovery=5/5 num=" << count
                  << " initial=" << act << " final=" << final_act
                  << " edit=0 cnf=false"
                  << (activate ? " activation=verified" : "")
                  << (reject ? " rejection=verified" : "") << '\n';
        session.disconnect();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SGCB_PROBE_FAIL " << error.what() << '\n';
        return 1;
    }
}
''')

# Root build exposes the reusable acceptance client.
replace(
    "CMakeLists.txt",
    '''    add_executable(ariec61850_mms_brcb_event_probe tools/mms_brcb_event_probe.cpp)\n    target_link_libraries(ariec61850_mms_brcb_event_probe PRIVATE ARIEC61850::core)\n    target_compile_features(ariec61850_mms_brcb_event_probe PRIVATE cxx_std_20)\n    ariec61850_apply_warnings(ariec61850_mms_brcb_event_probe)\n    ariec61850_apply_sanitizers(ariec61850_mms_brcb_event_probe)\n\n''',
    '''    add_executable(ariec61850_mms_brcb_event_probe tools/mms_brcb_event_probe.cpp)\n    target_link_libraries(ariec61850_mms_brcb_event_probe PRIVATE ARIEC61850::core)\n    target_compile_features(ariec61850_mms_brcb_event_probe PRIVATE cxx_std_20)\n    ariec61850_apply_warnings(ariec61850_mms_brcb_event_probe)\n    ariec61850_apply_sanitizers(ariec61850_mms_brcb_event_probe)\n\n    add_executable(ariec61850_mms_sgcb_probe tools/mms_sgcb_probe.cpp)\n    target_link_libraries(ariec61850_mms_sgcb_probe PRIVATE ARIEC61850::core)\n    target_compile_features(ariec61850_mms_sgcb_probe PRIVATE cxx_std_20)\n    ariec61850_apply_warnings(ariec61850_mms_sgcb_probe)\n    ariec61850_apply_sanitizers(ariec61850_mms_sgcb_probe)\n\n''')

# App-side helper QA and build dependency on the reusable field probe.
replace(
    "apps/ied_simulator/CMakeLists.txt",
    '''add_executable(ied_simulator_guardrails_qa\n''',
    '''add_executable(ied_setting_control_manifest_qa\n    src/ied_setting_control_manifest_qa.cpp)\ntarget_link_libraries(ied_setting_control_manifest_qa PRIVATE\n    Qt6::Core)\ntarget_include_directories(ied_setting_control_manifest_qa PRIVATE\n    ${CMAKE_CURRENT_LIST_DIR}/src)\ntarget_compile_features(ied_setting_control_manifest_qa PRIVATE cxx_std_20)\n\nadd_executable(ied_simulator_guardrails_qa\n''')
replace(
    "apps/ied_simulator/CMakeLists.txt",
    '''    ariec61850_mms_brcb_event_probe\n    ariec61850_control_interop_probe)\n''',
    '''    ariec61850_mms_brcb_event_probe\n    ariec61850_mms_sgcb_probe\n    ariec61850_control_interop_probe)\n''')

# 3) Production static server: SGCB manifest parsing, canonical shared state,
# capacity accounting, per-association object-bank composition and observability.
replace(
    "tools/static_ied_server.cpp",
    '#include "ariec61850/mms/static_direct_control.hpp"\n',
    '#include "ariec61850/mms/static_direct_control.hpp"\n#include "ariec61850/mms/static_setting_group.hpp"\n#include "ariec61850/mms/utc_time.hpp"\n')

replace(
    "tools/static_ied_server.cpp",
    '''struct ManifestDirectControlStorage final {\n''',
    '''struct ManifestSettingGroupStorage final {\n    std::string domain;\n    std::string item;\n    std::uint32_t number_of_groups{};\n    std::uint32_t active_group{};\n    std::shared_ptr<mms::MmsStaticSettingGroupSharedState> shared_state;\n};\n\nstruct ManifestDirectControlStorage final {\n''')
replace(
    "tools/static_ied_server.cpp",
    '''    std::vector<ManifestDirectControlStorage> direct_control_storage;\n    std::size_t omitted_direct_controls{};\n''',
    '''    std::vector<ManifestDirectControlStorage> direct_control_storage;\n    std::size_t omitted_direct_controls{};\n    std::vector<ManifestSettingGroupStorage> setting_group_storage;\n''')

replace(
    "tools/static_ied_server.cpp",
    '''struct BrcbAssociationRuntime final {\n''',
    '''struct SettingGroupAssociationRuntime final {\n    mms::MmsStaticSettingGroupDefinition definition;\n    std::vector<mms::MmsStaticObjectEntry> object_storage;\n    std::array<mms::MmsStaticSettingGroupObjectContext,\n        mms::MmsStaticSettingGroupObjectBank::attributes_per_control_block> context_storage{};\n    std::vector<char> name_storage;\n    std::unique_ptr<mms::MmsStaticSettingGroupObjectBank> bank;\n};\n\nstruct BrcbAssociationRuntime final {\n''')

replace(
    "tools/static_ied_server.cpp",
    '''[[nodiscard]] std::uint64_t report_now_ms(const void*) noexcept {\n    return monotonic_ms();\n}\n\n''',
    '''[[nodiscard]] std::uint64_t report_now_ms(const void*) noexcept {\n    return monotonic_ms();\n}\n\n[[nodiscard]] bool setting_group_utc_now(\n    const void*,\n    const std::span<std::uint8_t, 8U> destination) noexcept {\n    return mms::Iec61850UtcTime{std::chrono::system_clock::now(), 0U}\n        .try_write_bytes(destination);\n}\n\n''')

replace(
    "tools/static_ied_server.cpp",
    '''    struct ParsedReportControl final {\n''',
    '''    struct ParsedSettingGroup final {\n        std::string domain;\n        std::string item;\n        std::uint32_t number_of_groups{};\n        std::uint32_t active_group{};\n    };\n    struct ParsedReportControl final {\n''')
replace(
    "tools/static_ied_server.cpp",
    '''    std::vector<ParsedDataSetMember> parsed_members;\n    std::vector<ParsedReportControl> parsed_reports;\n''',
    '''    std::vector<ParsedDataSetMember> parsed_members;\n    std::vector<ParsedSettingGroup> parsed_setting_groups;\n    std::vector<ParsedReportControl> parsed_reports;\n''')
replace(
    "tools/static_ied_server.cpp",
    '''            parsed_reports.push_back(std::move(report));\n        }\n    }\n    if (roots.empty()) {\n''',
    '''            parsed_reports.push_back(std::move(report));\n        } else if (fields.size() >= 5U && fields[0] == "SGCB") {\n            if (fields[1].empty() || fields[2].empty()) {\n                throw std::runtime_error("Model manifest contains a malformed SGCB entry.");\n            }\n            ParsedSettingGroup setting;\n            setting.domain = fields[1];\n            setting.item = fields[2];\n            setting.number_of_groups = parse_u32("SGCB NumOfSG", fields[3], 0xFFU);\n            setting.active_group = parse_u32("SGCB ActSG", fields[4], 0xFFU);\n            if (setting.number_of_groups == 0U || setting.active_group == 0U ||\n                setting.active_group > setting.number_of_groups) {\n                throw std::runtime_error(\n                    "Model manifest SGCB requires 1 <= ActSG <= NumOfSG <= 255.");\n            }\n            parsed_setting_groups.push_back(std::move(setting));\n        }\n    }\n    if (roots.empty()) {\n''')

# Compile each manifest SGCB exactly once. Shared state is created at server-model
# scope; local per-association copies are rebound to it below.
replace(
    "tools/static_ied_server.cpp",
    '''        model.direct_control_storage.push_back(std::move(control));\n    }\n\n    const auto ensure_data_set_member_object = [&](const ParsedDataSetMember& member) {\n''',
    '''        model.direct_control_storage.push_back(std::move(control));\n    }\n\n    std::set<std::pair<std::string, std::string>> unique_setting_groups;\n    model.setting_group_storage.reserve(parsed_setting_groups.size());\n    for (const auto& parsed : parsed_setting_groups) {\n        if (!unique_setting_groups.emplace(parsed.domain, parsed.item).second) {\n            throw std::runtime_error("Model manifest contains a duplicate SGCB entry.");\n        }\n        ManifestSettingGroupStorage setting;\n        setting.domain = parsed.domain;\n        setting.item = parsed.item;\n        setting.number_of_groups = parsed.number_of_groups;\n        setting.active_group = parsed.active_group;\n        setting.shared_state = std::make_shared<mms::MmsStaticSettingGroupSharedState>(\n            parsed.number_of_groups, parsed.active_group);\n        model.setting_group_storage.push_back(std::move(setting));\n    }\n\n    const auto ensure_data_set_member_object = [&](const ParsedDataSetMember& member) {\n''')

replace(
    "tools/static_ied_server.cpp",
    '''    if (model.objects.size() > mms::MmsStaticObjectTable::maximum_objects - control_service_objects) {\n        throw std::runtime_error("Configured controls exceed MMS object capacity.");\n    }\n    auto remaining_object_slots = mms::MmsStaticObjectTable::maximum_objects -\n        model.objects.size() - control_service_objects;\n''',
    '''    const auto setting_group_count = model.setting_group_storage.size();\n    if (setting_group_count >\n        mms::MmsStaticObjectTable::maximum_objects /\n            mms::MmsStaticSettingGroupObjectBank::attributes_per_control_block) {\n        throw std::runtime_error("Configured SGCBs exceed MMS object capacity.");\n    }\n    const auto setting_group_service_objects = setting_group_count *\n        mms::MmsStaticSettingGroupObjectBank::attributes_per_control_block;\n    if (control_service_objects >\n        mms::MmsStaticObjectTable::maximum_objects - setting_group_service_objects) {\n        throw std::runtime_error("Configured controls and SGCBs exceed MMS object capacity.");\n    }\n    const auto service_objects = control_service_objects + setting_group_service_objects;\n    if (model.objects.size() > mms::MmsStaticObjectTable::maximum_objects - service_objects) {\n        throw std::runtime_error("Configured controls and SGCBs exceed MMS object capacity.");\n    }\n    auto remaining_object_slots = mms::MmsStaticObjectTable::maximum_objects -\n        model.objects.size() - service_objects;\n''')

replace(
    "tools/static_ied_server.cpp",
    '''    std::unique_ptr<mms::MmsStaticObjectTable> direct_control_table;\n    std::unique_ptr<filehost::StaticFileServiceSession> file_session;\n''',
    '''    std::unique_ptr<mms::MmsStaticObjectTable> direct_control_table;\n    std::vector<std::unique_ptr<SettingGroupAssociationRuntime>> setting_group_runtimes;\n    std::unique_ptr<filehost::StaticFileServiceSession> file_session;\n''')

replace(
    "tools/static_ied_server.cpp",
    '''    const auto* process_objects = dispatch_objects;\n    if (manifest_model != nullptr && !manifest_model->urcb_definitions.empty()) {\n''',
    '''    if (manifest_model != nullptr && !manifest_model->setting_group_storage.empty()) {\n        setting_group_runtimes.reserve(manifest_model->setting_group_storage.size());\n        for (auto& setting : manifest_model->setting_group_storage) {\n            if (setting.shared_state == nullptr || !setting.shared_state->valid()) {\n                throw std::runtime_error("Configured SGCB has no valid canonical shared state.");\n            }\n            auto runtime = std::make_unique<SettingGroupAssociationRuntime>();\n            runtime->definition = mms::MmsStaticSettingGroupDefinition{\n                setting.domain, setting.item};\n            mms::MmsStaticSettingGroupObjectBank sizing_bank{\n                runtime->definition,\n                *setting.shared_state,\n                dispatch_objects->objects(),\n                std::span<mms::MmsStaticObjectEntry>{},\n                std::span<mms::MmsStaticSettingGroupObjectContext>{},\n                std::span<char>{},\n                setting_group_utc_now,\n                nullptr};\n            const auto required_objects = sizing_bank.required_object_capacity();\n            const auto required_contexts = sizing_bank.required_context_capacity();\n            const auto required_names = sizing_bank.required_name_bytes();\n            if (required_objects == std::numeric_limits<std::size_t>::max() ||\n                required_contexts == std::numeric_limits<std::size_t>::max() ||\n                required_names == std::numeric_limits<std::size_t>::max()) {\n                throw std::runtime_error("SGCB object-bank capacity calculation failed.");\n            }\n            runtime->object_storage.resize(required_objects);\n            runtime->name_storage.resize(required_names);\n            runtime->bank = std::make_unique<mms::MmsStaticSettingGroupObjectBank>(\n                runtime->definition,\n                *setting.shared_state,\n                dispatch_objects->objects(),\n                std::span<mms::MmsStaticObjectEntry>{runtime->object_storage},\n                std::span<mms::MmsStaticSettingGroupObjectContext>{runtime->context_storage},\n                std::span<char>{runtime->name_storage},\n                setting_group_utc_now,\n                nullptr);\n            if (!runtime->bank->initialize()) {\n                throw std::runtime_error("Could not expose SGCB MMS attribute objects.");\n            }\n            dispatch_objects = &runtime->bank->table();\n            setting_group_runtimes.push_back(std::move(runtime));\n        }\n        dispatch_policy.advertise_flattened_child_aliases = true;\n    }\n\n    const auto* process_objects = dispatch_objects;\n    if (manifest_model != nullptr && !manifest_model->urcb_definitions.empty()) {\n''')

replace(
    "tools/static_ied_server.cpp",
    '''        const auto exposed_objects = object_span.size() +\n            manifest_model.urcb_definitions.size() *\n                mms::MmsStaticUrcbObjectBank::attributes_per_control_block +\n''',
    '''        const auto exposed_objects = object_span.size() +\n            manifest_model.setting_group_storage.size() *\n                mms::MmsStaticSettingGroupObjectBank::attributes_per_control_block +\n            manifest_model.urcb_definitions.size() *\n                mms::MmsStaticUrcbObjectBank::attributes_per_control_block +\n''')
replace(
    "tools/static_ied_server.cpp",
    '''            << " datasets=" << data_set_span.size()\n            << " urcbs=" << manifest_model.urcb_definitions.size()\n''',
    '''            << " datasets=" << data_set_span.size()\n            << " sgcbs=" << manifest_model.setting_group_storage.size()\n            << " urcbs=" << manifest_model.urcb_definitions.size()\n''')

replace(
    "tools/static_ied_server.cpp",
    '''                        for (auto& local_control : local_model.direct_control_storage) {\n                            const auto shared = std::find_if(\n                                manifest_model.direct_control_storage.begin(),\n                                manifest_model.direct_control_storage.end(),\n                                [&](const auto& candidate) {\n                                    return candidate.domain == local_control.domain &&\n                                        candidate.status_item == local_control.status_item &&\n                                        candidate.control_model == local_control.control_model;\n                                });\n                            if (shared == manifest_model.direct_control_storage.end() ||\n                                shared->shared_state == nullptr) {\n                                throw std::runtime_error(\n                                    "Per-association control has no canonical shared state.");\n                            }\n                            local_control.shared_state = shared->shared_state;\n                        }\n\n''',
    '''                        for (auto& local_control : local_model.direct_control_storage) {\n                            const auto shared = std::find_if(\n                                manifest_model.direct_control_storage.begin(),\n                                manifest_model.direct_control_storage.end(),\n                                [&](const auto& candidate) {\n                                    return candidate.domain == local_control.domain &&\n                                        candidate.status_item == local_control.status_item &&\n                                        candidate.control_model == local_control.control_model;\n                                });\n                            if (shared == manifest_model.direct_control_storage.end() ||\n                                shared->shared_state == nullptr) {\n                                throw std::runtime_error(\n                                    "Per-association control has no canonical shared state.");\n                            }\n                            local_control.shared_state = shared->shared_state;\n                        }\n                        for (auto& local_setting : local_model.setting_group_storage) {\n                            const auto shared = std::find_if(\n                                manifest_model.setting_group_storage.begin(),\n                                manifest_model.setting_group_storage.end(),\n                                [&](const auto& candidate) {\n                                    return candidate.domain == local_setting.domain &&\n                                        candidate.item == local_setting.item;\n                                });\n                            if (shared == manifest_model.setting_group_storage.end() ||\n                                shared->shared_state == nullptr) {\n                                throw std::runtime_error(\n                                    "Per-association SGCB has no canonical shared state.");\n                            }\n                            local_setting.shared_state = shared->shared_state;\n                        }\n\n''')

print("Phase2B SGCB production wiring patch applied")
