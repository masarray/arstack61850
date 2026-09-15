    detect_address_conflicts("SV", document.sampled_values_streams);

    // Parse MMS/OSI addressing from the same bounded XML tree as every other SCL
    // projection. Do not make desktop/client code re-open XML to discover
    // ConnectedAP parameters.
    if (const auto* communication = first_child(root, "Communication"); communication != nullptr) {
        const auto parse_decimal_u32 = [](const std::string_view text)
            -> std::optional<std::uint32_t> {
            const auto cleaned = trim_copy(text);
            if (cleaned.empty()) return std::nullopt;
            std::uint32_t value{};
            const auto parsed = std::from_chars(
                cleaned.data(), cleaned.data() + cleaned.size(), value, 10);
            if (parsed.ec != std::errc{} || parsed.ptr != cleaned.data() + cleaned.size()) {
                return std::nullopt;
            }
            return value;
        };

        const auto parse_oid_arcs = [&](const std::string_view text)
            -> std::optional<std::vector<std::uint32_t>> {
            const auto cleaned = trim_copy(text);
            if (cleaned.empty()) return std::nullopt;
            std::vector<std::uint32_t> arcs;
            std::size_t offset{};
            while (offset < cleaned.size()) {
                while (offset < cleaned.size() &&
                       (std::isspace(static_cast<unsigned char>(cleaned[offset])) != 0 ||
                        cleaned[offset] == ',' || cleaned[offset] == '.')) {
                    ++offset;
                }
                if (offset >= cleaned.size()) break;
                const auto begin = offset;
                while (offset < cleaned.size() &&
                       std::isdigit(static_cast<unsigned char>(cleaned[offset])) != 0) {
                    ++offset;
                }
                if (begin == offset) return std::nullopt;
                std::uint32_t arc{};
                const auto parsed = std::from_chars(
                    cleaned.data() + static_cast<std::ptrdiff_t>(begin),
                    cleaned.data() + static_cast<std::ptrdiff_t>(offset),
                    arc,
                    10);
                if (parsed.ec != std::errc{}) return std::nullopt;
                arcs.push_back(arc);
                if (arcs.size() > 32U) return std::nullopt;
                while (offset < cleaned.size() &&
                       std::isspace(static_cast<unsigned char>(cleaned[offset])) != 0) {
                    ++offset;
                }
                if (offset < cleaned.size() && cleaned[offset] != ',' && cleaned[offset] != '.') {
                    return std::nullopt;
                }
            }
            if (arcs.size() < 2U || arcs[0] > 2U ||
                (arcs[0] < 2U && arcs[1] > 39U)) {
                return std::nullopt;
            }
            return arcs;
        };

        const auto parse_selector = [](const std::string_view text)
            -> std::optional<std::vector<std::uint8_t>> {
            auto cleaned = trim_copy(text);
            if (cleaned.empty()) return std::nullopt;
            if (cleaned.size() >= 2U && cleaned[0] == '0' &&
                (cleaned[1] == 'x' || cleaned[1] == 'X')) {
                cleaned.erase(0U, 2U);
            }
            std::string hex;
            hex.reserve(cleaned.size());
            for (const auto ch : cleaned) {
                if (std::isspace(static_cast<unsigned char>(ch)) != 0 ||
                    ch == ':' || ch == '-') {
                    continue;
                }
                if (std::isxdigit(static_cast<unsigned char>(ch)) == 0) {
                    return std::nullopt;
                }
                hex.push_back(ch);
            }
            if (hex.empty() || (hex.size() % 2U) != 0U || hex.size() / 2U > 64U) {
                return std::nullopt;
            }
            std::vector<std::uint8_t> bytes;
            bytes.reserve(hex.size() / 2U);
            for (std::size_t offset = 0U; offset < hex.size(); offset += 2U) {
                unsigned value{};
                const auto parsed = std::from_chars(
                    hex.data() + static_cast<std::ptrdiff_t>(offset),
                    hex.data() + static_cast<std::ptrdiff_t>(offset + 2U),
                    value,
                    16);
                if (parsed.ec != std::errc{} || value > 0xFFU) return std::nullopt;
                bytes.push_back(static_cast<std::uint8_t>(value));
            }
            return bytes;
        };

        for (const auto* connected_ap : descendants(*communication, "ConnectedAP")) {
            const auto* address = first_child(*connected_ap, "Address");
            if (address == nullptr) continue;

            std::unordered_map<std::string, std::string> parameters;
            for (const auto* parameter : direct_children(*address, "P")) {
                const auto key = normalized_key(attribute(parameter, "type"));
                if (!key.empty()) parameters[key] = trim_copy(parameter->text);
            }
            const auto get = [&parameters](const std::string_view type) {
                const auto found = parameters.find(normalized_key(type));
                return found == parameters.end() ? std::string{} : found->second;
            };
            const auto get_first = [&get](const std::initializer_list<std::string_view> names) {
                for (const auto name : names) {
                    auto value = get(name);
                    if (!value.empty()) return value;
                }
                return std::string{};
            };

            SclMmsAccessPoint endpoint;
            endpoint.ied_name = attribute(connected_ap, "iedName");
            endpoint.access_point_name = attribute(connected_ap, "apName");
            endpoint.ip_address = get("IP");

            const auto port_text = get_first({"IP-Port", "MMS-Port", "Port"});
            if (!port_text.empty()) {
                if (const auto parsed = parse_u16(port_text, false); parsed && *parsed != 0U) {
                    endpoint.tcp_port = *parsed;
                } else {
                    endpoint.association_parameters_valid = false;
                    endpoint.association_error = "Invalid MMS TCP port '" + port_text + "'.";
                }
            }

            const auto ap_title_text = get_first({"OSI-AP-Title", "AP-Title"});
            const auto ae_text = get_first({"OSI-AE-Qualifier", "AE-Qualifier"});
            const auto psel_text = get_first({"OSI-PSEL", "PSEL"});
            const auto ssel_text = get_first({"OSI-SSEL", "SSEL"});
            const auto tsel_text = get_first({"OSI-TSEL", "TSEL"});
            endpoint.association_parameters_present =
                !ap_title_text.empty() || !ae_text.empty() || !psel_text.empty() ||
                !ssel_text.empty() || !tsel_text.empty();

            const auto invalidate = [&endpoint](std::string message) {
                endpoint.association_parameters_valid = false;
                if (!endpoint.association_error.empty()) endpoint.association_error += " ";
                endpoint.association_error += std::move(message);
            };

            if (!ap_title_text.empty()) {
                const auto parsed = parse_oid_arcs(ap_title_text);
                if (parsed) endpoint.ap_title = *parsed;
                else invalidate("Invalid OSI AP-title '" + ap_title_text + "'.");
            }
            if (!ae_text.empty()) {
                const auto parsed = parse_decimal_u32(ae_text);
                if (parsed) endpoint.ae_qualifier = *parsed;
                else invalidate("Invalid OSI AE-qualifier '" + ae_text + "'.");
            }
            const auto assign_selector = [&](const std::string& text,
                                             const char* label,
                                             std::vector<std::uint8_t>& destination) {
                if (text.empty()) return;
                const auto parsed = parse_selector(text);
                if (parsed) destination = *parsed;
                else invalidate(std::string{"Invalid "} + label + " selector '" + text + "'.");
            };
            assign_selector(psel_text, "OSI P", endpoint.p_selector);
            assign_selector(ssel_text, "OSI S", endpoint.s_selector);
            assign_selector(tsel_text, "OSI T", endpoint.t_selector);

            if (!endpoint.association_parameters_valid) {
                document.warnings.push_back(
                    "MMS ConnectedAP " + endpoint.ied_name + "/" + endpoint.access_point_name +
                    " has invalid association addressing: " + endpoint.association_error);
            }
            document.mms_access_points.push_back(std::move(endpoint));
        }
    }

    // A focused engineering file may intentionally describe only one service
    // family (for example, only Sampled Values). Absence of unrelated GOOSE,
    // SV, or ReportControl blocks is therefore not a parser warning. Callers
    // that require a specific service should validate that requirement in
    // their own profile/compiler layer.
    if (document.data_sets.empty()) {
        document.warnings.push_back("No DataSet element was found. Semantic mapping will be limited.");
    }

    return document;
}

} // namespace ar::iec61850::scl