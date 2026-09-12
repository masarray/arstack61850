// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/simulation/ied_simulator_profile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace ar::iec61850::simulation {
namespace {

[[nodiscard]] char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

[[nodiscard]] bool ascii_equal(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) return false;
    }
    return true;
}

[[nodiscard]] bool ascii_ends_with(
    const std::string_view value,
    const std::string_view suffix) noexcept {
    if (suffix.size() > value.size()) return false;
    const auto offset = value.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        if (ascii_lower(value[offset + index]) != ascii_lower(suffix[index])) return false;
    }
    return true;
}

[[nodiscard]] bool ascii_contains(
    const std::string_view value,
    const std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > value.size()) return false;
    for (std::size_t offset = 0U; offset + needle.size() <= value.size(); ++offset) {
        bool match = true;
        for (std::size_t index = 0U; index < needle.size(); ++index) {
            if (ascii_lower(value[offset + index]) != ascii_lower(needle[index])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

[[nodiscard]] std::string replace_dots(std::string value, const char replacement) {
    std::replace(value.begin(), value.end(), '.', replacement);
    return value;
}

[[nodiscard]] std::string logical_node_name(const scl::SclDataSetEntry& entry) {
    return entry.prefix + entry.ln_class + entry.ln_inst;
}

[[nodiscard]] std::string relative_reference(const scl::SclDataSetEntry& entry) {
    std::string result = logical_node_name(entry);
    if (!entry.do_name.empty()) result += "." + entry.do_name;
    if (!entry.da_name.empty()) result += "." + entry.da_name;
    return result;
}

[[nodiscard]] std::string runtime_domain(
    const scl::SclDataSetEntry& entry,
    const std::string& source_ied_name,
    const std::string& runtime_ied_name) {
    const auto ied = entry.ied_name.empty() || ascii_equal(entry.ied_name, source_ied_name)
        ? runtime_ied_name
        : entry.ied_name;
    return ied + entry.ld_inst;
}

[[nodiscard]] std::string mms_item(const scl::SclDataSetEntry& entry) {
    auto data_object = replace_dots(entry.do_name, '$');
    auto data_attribute = replace_dots(entry.da_name, '$');
    std::string result = logical_node_name(entry);
    if (!entry.functional_constraint.empty()) result += "$" + entry.functional_constraint;
    if (!data_object.empty()) result += "$" + data_object;
    if (!data_attribute.empty()) result += "$" + data_attribute;
    return result;
}

[[nodiscard]] std::string source_stem(const std::string& source_name) {
    auto name = source_name;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0U, slash + 1U);
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos && dot != 0U) name.erase(dot);
    return name;
}

[[nodiscard]] bool document_has_ied(
    const scl::SclDocument& document,
    const std::string& name) {
    if (name.empty()) return false;
    if (std::any_of(
            document.ieds.begin(), document.ieds.end(),
            [&name](const scl::SclIed& ied) { return ascii_equal(ied.name, name); })) {
        return true;
    }
    return std::any_of(
        document.model_entries.begin(), document.model_entries.end(),
        [&name](const scl::SclDataSetEntry& entry) {
            return ascii_equal(entry.ied_name, name);
        });
}

[[nodiscard]] std::string resolve_source_ied(
    const scl::SclDocument& document,
    const IedSimulatorProfileFromSclOptions& options,
    std::vector<std::string>& findings) {
    if (!options.ied_name.empty()) {
        if (document_has_ied(document, options.ied_name)) return options.ied_name;
        findings.push_back(
            "Requested IED '" + options.ied_name +
            "' was not found; the first available IED was selected.");
    }
    if (!document.ieds.empty()) return document.ieds.front().name;
    for (const auto& entry : document.model_entries) {
        if (!entry.ied_name.empty()) return entry.ied_name;
    }
    for (const auto& data_set : document.data_sets) {
        if (!data_set.ied_name.empty()) return data_set.ied_name;
    }
    return {};
}

[[nodiscard]] std::string resolve_runtime_ied(
    const scl::SclDocument& document,
    const std::string& source_ied,
    const IedSimulatorProfileFromSclOptions& options,
    std::vector<std::string>& findings) {
    if (!options.runtime_ied_name.empty()) return options.runtime_ied_name;
    if (ascii_equal(source_ied, "TEMPLATE")) {
        const auto stem = source_stem(document.source_name);
        if (!stem.empty()) {
            findings.push_back(
                "Instantiated TEMPLATE IED as runtime IED '" + stem + "'.");
            return stem;
        }
    }
    return source_ied;
}

[[nodiscard]] std::string remap_reference(
    std::string reference,
    const std::string& source_ied,
    const std::string& runtime_ied) {
    if (source_ied.empty() || runtime_ied.empty() || ascii_equal(source_ied, runtime_ied)) {
        return reference;
    }
    if (reference.size() >= source_ied.size() &&
        ascii_equal(std::string_view{reference}.substr(0U, source_ied.size()), source_ied)) {
        reference.replace(0U, source_ied.size(), runtime_ied);
    }
    return reference;
}

[[nodiscard]] bool matches_ied(
    const std::string& candidate,
    const std::string& source_ied) noexcept {
    return candidate.empty() || ascii_equal(candidate, source_ied);
}

[[nodiscard]] std::string display_type(const scl::SclDataSetEntry& entry) {
    if (entry.is_quality || ascii_ends_with(entry.da_name, "q") ||
        ascii_equal(entry.basic_type, "Quality")) {
        return "Quality";
    }
    if (entry.is_timestamp || ascii_ends_with(entry.da_name, "t") ||
        ascii_equal(entry.basic_type, "Timestamp")) {
        return "Timestamp";
    }
    if (ascii_contains(entry.basic_type, "Bool")) return "Boolean";
    if (!entry.enum_type.empty() || ascii_contains(entry.basic_type, "Enum")) {
        return "Enumeration";
    }
    if (ascii_contains(entry.basic_type, "Float") ||
        ascii_contains(entry.basic_type, "INT") ||
        ascii_contains(entry.basic_type, "Integer")) {
        return "Number";
    }
    if (ascii_equal(entry.cdc, "DPC") && entry.basic_type.empty()) {
        return "Enumeration";
    }
    return entry.basic_type.empty() ? "Text" : entry.basic_type;
}

[[nodiscard]] std::optional<int> control_model_code(std::string value) {
    std::string token;
    token.reserve(value.size());
    for (const auto character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            token.push_back(ascii_lower(character));
        }
    }
    if (token == "0" || token == "statusonly") return 0;
    if (token == "1" || token == "directwithnormalsecurity" || token == "directnormal") return 1;
    if (token == "2" || token == "sbowithnormalsecurity" ||
        token == "selectbeforeoperatewithnormalsecurity" || token == "sbonormal") return 2;
    if (token == "3" || token == "directwithenhancedsecurity" || token == "directenhanced") return 3;
    if (token == "4" || token == "sbowithenhancedsecurity" ||
        token == "selectbeforeoperatewithenhancedsecurity" || token == "sboenhanced") return 4;
    return std::nullopt;
}

[[nodiscard]] bool measurement_cdc(const std::string& cdc) noexcept {
    static constexpr std::string_view measurement_cdcs[] = {
        "MV", "CMV", "WYE", "DEL", "SEQ", "SAV", "ASG", "BCR", "HMV", "HWYE", "HDEL"};
    return std::any_of(
        std::begin(measurement_cdcs), std::end(measurement_cdcs),
        [&cdc](const std::string_view candidate) { return ascii_equal(cdc, candidate); });
}

[[nodiscard]] bool is_measurement(const scl::SclDataSetEntry& entry) noexcept {
    return ascii_equal(entry.functional_constraint, "MX") || measurement_cdc(entry.cdc);
}

[[nodiscard]] bool is_integer_type(const std::string& basic_type) noexcept {
    return ascii_contains(basic_type, "INT") || ascii_contains(basic_type, "Integer");
}

struct MeasurementShape final {
    double base_value{};
    double amplitude{};
    std::string unit;
};

[[nodiscard]] MeasurementShape measurement_shape(const scl::SclDataSetEntry& entry) {
    const bool voltage = ascii_equal(entry.ln_class, "TVTR") ||
        ascii_contains(entry.do_name, "Vol") || ascii_equal(entry.do_name, "PhV") ||
        ascii_equal(entry.do_name, "PPV");
    const bool current = ascii_equal(entry.ln_class, "TCTR") ||
        ascii_contains(entry.do_name, "Amp") || ascii_equal(entry.do_name, "A");
    const bool integer = is_integer_type(entry.basic_type);
    if (voltage) return integer ? MeasurementShape{0.0, 100000.0, "V"}
                                : MeasurementShape{230000.0, 1500.0, "V"};
    if (current) return integer ? MeasurementShape{0.0, 10000.0, "A"}
                                : MeasurementShape{240.0, 18.0, "A"};
    return integer ? MeasurementShape{0.0, 1000.0, {}}
                   : MeasurementShape{0.0, 1.0, {}};
}

[[nodiscard]] double phase_degrees(const scl::SclDataSetEntry& entry) noexcept {
    const auto reference = relative_reference(entry);
    if (ascii_contains(reference, "phsB") || ascii_contains(reference, "phaseB")) return 120.0;
    if (ascii_contains(reference, "phsC") || ascii_contains(reference, "phaseC")) return 240.0;
    return 0.0;
}

[[nodiscard]] bool dynamic_measurement(const scl::SclDataSetEntry& entry) noexcept {
    return ascii_ends_with(entry.da_name, "cVal.mag.f") ||
        ascii_ends_with(entry.da_name, "instMag.f");
}

[[nodiscard]] std::string format_number(const double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(3) << value;
    auto result = stream.str();
    while (!result.empty() && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result.empty() ? std::string{"0"} : result;
}

[[nodiscard]] std::string initial_value(
    const scl::SclDataSetEntry& entry,
    const std::string& type,
    const MeasurementShape& shape,
    const bool measurement) {
    if (!entry.configured_value.empty()) {
        if (ascii_equal(entry.da_name, "ctlModel")) {
            if (const auto code = control_model_code(entry.configured_value); code.has_value()) {
                return std::to_string(*code);
            }
        }
        return entry.configured_value;
    }
    if (type == "Quality") return "good";
    if (type == "Timestamp") return "0";
    if (measurement) return format_number(shape.base_value);
    if (type == "Boolean") return "false";
    if (type == "Enumeration") return "0";
    if (type == "Number") return "0";
    return "";
}

[[nodiscard]] IedSimulatorPoint make_point(
    const scl::SclDataSetEntry& entry,
    const std::string& source_ied,
    const std::string& runtime_ied) {
    IedSimulatorPoint point;
    point.ied_name = runtime_ied;
    point.logical_device = entry.ld_inst;
    point.logical_node = logical_node_name(entry);
    point.ln_class = entry.ln_class;
    point.data_object = entry.do_name;
    point.data_attribute = entry.da_name;
    point.functional_constraint = entry.functional_constraint;
    point.cdc = entry.cdc;
    point.basic_type = entry.basic_type;
    point.enum_type = entry.enum_type;
    point.mms_domain = runtime_domain(entry, source_ied, runtime_ied);
    point.mms_item = mms_item(entry);
    point.relative_reference = relative_reference(entry);
    point.reference = point.mms_domain + "/" + point.relative_reference;
    point.display_type = display_type(entry);

    const auto shape = measurement_shape(entry);
    const bool measurement = is_measurement(entry) &&
        point.display_type != "Quality" && point.display_type != "Timestamp";
    point.base_value = shape.base_value;
    point.amplitude = shape.amplitude;
    point.phase_degrees = phase_degrees(entry);
    point.unit = shape.unit;
    point.dynamic = measurement && dynamic_measurement(entry);
    if (point.display_type == "Quality") point.kind = SimulatorPointKind::quality;
    else if (point.display_type == "Timestamp") point.kind = SimulatorPointKind::timestamp;
    else if (measurement) point.kind = SimulatorPointKind::measurement;
    else point.kind = SimulatorPointKind::status;
    point.initial_value = initial_value(entry, point.display_type, shape, measurement);
    return point;
}

struct NodeBuilder final {
    std::string name;
    std::string ln_class;
    std::vector<IedSimulatorPoint> points;
};

struct DeviceBuilder final {
    std::string name;
    std::string ld_inst;
    std::map<std::string, NodeBuilder> nodes;
};

[[nodiscard]] std::string edition_name(const scl::SclEdition edition) {
    switch (edition) {
    case scl::SclEdition::edition1: return "IEC 61850 Edition 1";
    case scl::SclEdition::edition2: return "IEC 61850 Edition 2";
    case scl::SclEdition::edition21: return "IEC 61850 Edition 2.1";
    case scl::SclEdition::unknown: break;
    }
    return "IEC 61850 edition unknown";
}

void ensure_node(
    std::map<std::string, DeviceBuilder>& devices,
    const std::string& domain,
    const std::string& ld_inst,
    const std::string& node_name,
    const std::string& ln_class) {
    auto [device_it, inserted] = devices.try_emplace(domain);
    if (inserted) {
        device_it->second.name = domain;
        device_it->second.ld_inst = ld_inst;
    }
    auto [node_it, node_inserted] = device_it->second.nodes.try_emplace(node_name);
    if (node_inserted) {
        node_it->second.name = node_name;
        node_it->second.ln_class = ln_class;
    }
}

[[nodiscard]] std::string data_set_item(const scl::SclDataSet& data_set) {
    auto logical_node = replace_dots(data_set.logical_node_path, '$');
    if (logical_node.empty()) logical_node = "LLN0";
    return logical_node + "$" + data_set.name;
}

[[nodiscard]] std::string report_item(const scl::SclReportControl& report) {
    auto logical_node = replace_dots(report.logical_node_path, '$');
    if (logical_node.empty()) logical_node = "LLN0";
    return logical_node + (report.buffered ? "$BR$" : "$RP$") + report.name;
}

} // namespace

std::string IedSimulatorReportControlBlock::mode() const {
    return buffered ? "BRCB" : "URCB";
}

std::size_t IedSimulatorProfile::logical_node_count() const noexcept {
    std::size_t count{};
    for (const auto& device : logical_devices) count += device.logical_nodes.size();
    return count;
}

std::size_t IedSimulatorProfile::point_count() const noexcept {
    std::size_t count{};
    for (const auto& device : logical_devices) {
        for (const auto& node : device.logical_nodes) count += node.points.size();
    }
    return count;
}

std::size_t IedSimulatorProfile::data_set_member_count() const noexcept {
    std::size_t count{};
    for (const auto& data_set : data_sets) count += data_set.members.size();
    return count;
}

IedSimulatorProfile IedSimulatorProfile::create_default_feeder_profile() {
    IedSimulatorProfile profile;
    profile.name = "AR Demo Feeder IED";

    IedSimulatorLogicalDevice device;
    device.name = "IED1LD0";
    device.ld_inst = "LD0";

    IedSimulatorLogicalNode mmxu;
    mmxu.name = "MMXU1";
    mmxu.ln_class = "MMXU";
    for (const auto& spec : std::vector<std::pair<std::string, double>>{
             {"PhV.phsA.cVal.mag.f", 0.0},
             {"PhV.phsB.cVal.mag.f", 120.0},
             {"PhV.phsC.cVal.mag.f", 240.0}}) {
        IedSimulatorPoint point;
        point.reference = "IED1LD0/MMXU1." + spec.first;
        point.relative_reference = "MMXU1." + spec.first;
        point.ied_name = "IED1";
        point.logical_device = "LD0";
        point.logical_node = "MMXU1";
        point.ln_class = "MMXU";
        point.data_object = "PhV";
        point.data_attribute = spec.first.substr(4U);
        point.functional_constraint = "MX";
        point.cdc = "MV";
        point.basic_type = "FLOAT32";
        point.mms_domain = "IED1LD0";
        point.mms_item = "MMXU1$MX$" + replace_dots(spec.first, '$');
        point.display_type = "Number";
        point.unit = "V";
        point.initial_value = "230000";
        point.base_value = 230000.0;
        point.amplitude = 1500.0;
        point.phase_degrees = spec.second;
        point.dynamic = true;
        point.kind = SimulatorPointKind::measurement;
        mmxu.points.push_back(std::move(point));
    }

    IedSimulatorLogicalNode xcbr;
    xcbr.name = "XCBR1";
    xcbr.ln_class = "XCBR";
    IedSimulatorPoint position;
    position.reference = "IED1LD0/XCBR1.Pos.stVal";
    position.relative_reference = "XCBR1.Pos.stVal";
    position.ied_name = "IED1";
    position.logical_device = "LD0";
    position.logical_node = "XCBR1";
    position.ln_class = "XCBR";
    position.data_object = "Pos";
    position.data_attribute = "stVal";
    position.functional_constraint = "ST";
    position.cdc = "DPC";
    position.basic_type = "BOOLEAN";
    position.mms_domain = "IED1LD0";
    position.mms_item = "XCBR1$ST$Pos$stVal";
    position.display_type = "Boolean";
    position.initial_value = "closed";
    position.kind = SimulatorPointKind::status;
    xcbr.points.push_back(std::move(position));

    device.logical_nodes.push_back(std::move(mmxu));
    device.logical_nodes.push_back(std::move(xcbr));
    profile.logical_devices.push_back(std::move(device));
    return profile;
}

IedSimulatorProfileFromSclResult IedSimulatorProfileBuilder::build(
    const scl::SclDocument& document,
    const IedSimulatorProfileFromSclOptions& options) {
    IedSimulatorProfileFromSclResult result;
    result.source_ied_name = resolve_source_ied(document, options, result.findings);
    result.selected_ied_name = resolve_runtime_ied(
        document, result.source_ied_name, options, result.findings);

    if (result.source_ied_name.empty()) {
        result.findings.push_back("The SCL document contains no selectable IED identity.");
        result.profile.edition = "Imported from SCL (" + edition_name(document.edition) + ")";
        return result;
    }

    const auto& source_ied = result.source_ied_name;
    const auto& runtime_ied = result.selected_ied_name;
    result.profile.name = runtime_ied.empty() ? "SCL IED" : runtime_ied;
    result.profile.edition = "Imported from SCL (" + edition_name(document.edition) + ")";
    const auto ied_it = std::find_if(
        document.ieds.begin(), document.ieds.end(),
        [&source_ied](const scl::SclIed& ied) { return ascii_equal(ied.name, source_ied); });
    if (ied_it != document.ieds.end() && !ied_it->manufacturer.empty()) {
        result.profile.vendor = ied_it->manufacturer;
    }

    std::map<std::string, DeviceBuilder> devices;
    for (const auto& logical_node : document.logical_nodes) {
        if (!matches_ied(logical_node.ied_name, source_ied)) continue;
        const auto domain = runtime_ied + logical_node.ld_inst;
        ensure_node(
            devices, domain, logical_node.ld_inst,
            logical_node.name, logical_node.ln_class);
    }

    std::set<std::string> point_keys;
    std::size_t source_order{};
    const auto append_entry = [&](const scl::SclDataSetEntry& entry, const bool structural) {
        if (!matches_ied(entry.ied_name, source_ied)) return;
        if (!options.include_quality_and_timestamp_points &&
            (entry.is_quality || entry.is_timestamp || ascii_equal(entry.basic_type, "Quality") ||
             ascii_equal(entry.basic_type, "Timestamp"))) {
            return;
        }
        if (entry.functional_constraint.empty()) {
            result.findings.push_back(
                "Skipped SCL data attribute without a functional constraint: " +
                relative_reference(entry));
            return;
        }
        auto point = make_point(entry, source_ied, runtime_ied);
        point.source_order = source_order++;
        const auto key = point.mms_domain + "\n" + point.mms_item;
        if (!point_keys.insert(key).second) return;
        ensure_node(
            devices, point.mms_domain, entry.ld_inst,
            point.logical_node, point.ln_class);
        devices.at(point.mms_domain).nodes.at(point.logical_node).points.push_back(std::move(point));
        if (structural) ++result.structural_data_attribute_count;
    };

    if (!document.model_entries.empty()) {
        for (const auto& entry : document.model_entries) append_entry(entry, true);
    } else {
        result.findings.push_back(
            "The SCL parser exposed no structural model entries; service bindings were used as a compatibility fallback.");
        for (const auto& data_set : document.data_sets) {
            for (const auto& entry : data_set.entries) append_entry(entry, false);
        }
        for (const auto& stream : document.goose_streams) {
            for (const auto& entry : stream.entries) append_entry(entry, false);
        }
        for (const auto& report : document.report_controls) {
            for (const auto& entry : report.entries) append_entry(entry, false);
        }
    }

    for (auto& [domain, device] : devices) {
        IedSimulatorLogicalDevice output_device;
        output_device.name = domain;
        output_device.ld_inst = device.ld_inst;
        for (auto& [node_name, node] : device.nodes) {
            (void)node_name;
            std::sort(
                node.points.begin(), node.points.end(),
                [](const IedSimulatorPoint& left, const IedSimulatorPoint& right) {
                    return left.mms_item < right.mms_item;
                });
            output_device.logical_nodes.push_back(IedSimulatorLogicalNode{
                std::move(node.name), std::move(node.ln_class), std::move(node.points)});
        }
        result.profile.logical_devices.push_back(std::move(output_device));
    }

    for (const auto& data_set : document.data_sets) {
        if (!matches_ied(data_set.ied_name, source_ied)) continue;
        IedSimulatorDataSet output;
        output.reference = remap_reference(data_set.reference, source_ied, runtime_ied);
        output.mms_domain = runtime_ied + data_set.ld_inst;
        output.mms_item = data_set_item(data_set);
        for (const auto& entry : data_set.entries) {
            ++result.data_set_member_count;
            if (!options.include_quality_and_timestamp_points &&
                (entry.is_quality || entry.is_timestamp || ascii_equal(entry.basic_type, "Quality") ||
                 ascii_equal(entry.basic_type, "Timestamp"))) {
                ++result.skipped_member_count;
                continue;
            }
            output.members.push_back(
                runtime_domain(entry, source_ied, runtime_ied) + "/" + relative_reference(entry));
        }
        if (!output.members.empty()) result.profile.data_sets.push_back(std::move(output));
    }

    for (const auto& report : document.report_controls) {
        if (!matches_ied(report.ied_name, source_ied)) continue;
        IedSimulatorReportControlBlock output;
        output.reference = remap_reference(report.control_block_reference, source_ied, runtime_ied);
        output.mms_domain = runtime_ied + report.ld_inst;
        output.mms_item = report_item(report);
        output.buffered = report.buffered;
        output.data_set_reference = remap_reference(report.data_set_reference, source_ied, runtime_ied);
        output.report_id = report.report_id.empty() ? report.name : report.report_id;
        output.configuration_revision = report.configuration_revision;
        output.buffer_time_milliseconds = report.buffer_time_milliseconds;
        output.integrity_period_milliseconds = report.integrity_period_milliseconds;
        output.trigger_options = report.buffered
            ? "data-change, quality-change, integrity, GI"
            : "data-change, quality-change, GI";
        output.optional_fields = report.buffered
            ? "seqNum, entryId, timeStamp, reasonCode, dataSet, confRev"
            : "seqNum, timeStamp, reasonCode, dataSet, confRev";
        result.profile.report_control_blocks.push_back(std::move(output));
    }

    if (result.profile.logical_devices.empty()) {
        result.findings.push_back("The selected IED produced no logical devices.");
    }
    if (result.profile.point_count() == 0U) {
        result.findings.push_back("The selected IED produced no readable simulator points.");
    }
    if (result.profile.data_sets.empty()) {
        result.findings.push_back(
            "No DataSet definitions were found for the selected IED; only the structural model is exposed.");
    }

    return result;
}

} // namespace ar::iec61850::simulation
