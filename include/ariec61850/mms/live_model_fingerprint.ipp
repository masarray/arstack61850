// SPDX-License-Identifier: GPL-3.0-or-later

namespace ar::iec61850::mms {
namespace live_model_fingerprint_detail {

[[nodiscard]] inline std::uint64_t fnv1a64(const std::string_view text) noexcept {
    std::uint64_t value{14695981039346656037ULL};
    for (const char raw_byte : text) {
        const auto byte = static_cast<unsigned char>(raw_byte);
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}

[[nodiscard]] inline std::string hex64(const std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

[[nodiscard]] inline std::string normalized_bool(const std::optional<bool> value) {
    if (!value.has_value()) return "-";
    return *value ? "1" : "0";
}

} // namespace live_model_fingerprint_detail

inline std::string MmsLiveModelDocument::structural_manifest() const {
    using namespace live_model_detail;
    std::vector<std::string> lines;
    lines.push_back("ARIEC61850-LIVE-STRUCTURE|1");
    lines.push_back("IDENTITY|" + identity.ied_name);

    for (const auto& ld : logical_devices) {
        lines.push_back("LD|" + ld.mms_domain);
        for (const auto& ln : ld.logical_nodes) {
            lines.push_back(
                "LN|" + ld.mms_domain + "|" + ln.name + "|" +
                ln.logical_node_class);
            for (const auto& object : ln.data_objects) {
                lines.push_back("DO|" + object.reference);
                for (const auto& attribute : object.attributes) {
                    lines.push_back(
                        "DA|" + attribute.object_reference + "|" +
                        attribute.functional_constraint);
                }
            }
        }
    }

    for (const auto& control : report_controls) {
        lines.push_back(
            "RCB|" + control.reference + "|" +
            (control.buffered ? std::string{"B"} : std::string{"U"}));
    }

    const auto append_control_blocks = [&lines](const auto& controls) {
        for (const auto& control : controls) {
            lines.push_back(
                "CB|" + control.kind + "|" + control.reference + "|" +
                control.functional_constraint);
        }
    };
    append_control_blocks(goose_control_blocks);
    append_control_blocks(sampled_value_control_blocks);
    append_control_blocks(setting_group_controls);
    append_control_blocks(log_controls);

    std::sort(lines.begin() + 2, lines.end(), [](const auto& left, const auto& right) {
        return lower(left) < lower(right);
    });

    std::ostringstream out;
    for (const auto& line : lines) {
        out << line << '\n';
    }
    return out.str();
}

inline std::uint64_t MmsLiveModelDocument::structural_fingerprint() const {
    return live_model_fingerprint_detail::fnv1a64(structural_manifest());
}

inline std::string MmsLiveModelDocument::structural_fingerprint_hex() const {
    return live_model_fingerprint_detail::hex64(structural_fingerprint());
}

inline std::string MmsLiveModelDocument::runtime_snapshot_manifest() const {
    using namespace live_model_detail;
    using namespace live_model_fingerprint_detail;
    std::vector<std::string> lines;
    lines.push_back("ARIEC61850-LIVE-RUNTIME|1");
    lines.push_back("IDENTITY|" + identity.ied_name);

    for (const auto& data_set : data_sets) {
        std::string line =
            "DS|" + data_set.reference + "|" + normalized_bool(data_set.deletable) +
            "|" + std::to_string(data_set.members.size());
        for (const auto& member : data_set.members) {
            line += "|" + member.reference + "[" + member.functional_constraint + "]";
        }
        lines.push_back(std::move(line));
    }

    for (const auto& control : report_controls) {
        lines.push_back(
            "RCB|" + control.reference + "|" + control.data_set_binding_status + "|" +
            control.data_set_reference + "|" + control.enabled_state + "|" +
            control.reservation_state + "|" + control.reservation_time_seconds + "|" +
            control.report_id + "|" + control.configuration_revision);
    }

    std::sort(lines.begin() + 2, lines.end(), [](const auto& left, const auto& right) {
        return lower(left) < lower(right);
    });

    std::ostringstream out;
    for (const auto& line : lines) {
        out << line << '\n';
    }
    return out.str();
}

inline std::uint64_t MmsLiveModelDocument::runtime_snapshot_fingerprint() const {
    return live_model_fingerprint_detail::fnv1a64(runtime_snapshot_manifest());
}

inline std::string MmsLiveModelDocument::runtime_snapshot_fingerprint_hex() const {
    return live_model_fingerprint_detail::hex64(runtime_snapshot_fingerprint());
}

} // namespace ar::iec61850::mms
