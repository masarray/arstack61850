[[nodiscard]] inline bool starts_with_ci(
    const std::string_view value,
    const std::string_view prefix) {
    return value.size() >= prefix.size() && same(value.substr(0U, prefix.size()), prefix);
}
[[nodiscard]] inline bool ends_with_ci(
    const std::string_view value,
    const std::string_view suffix) {
    return value.size() >= suffix.size() &&
        same(value.substr(value.size() - suffix.size()), suffix);
}
[[nodiscard]] inline std::string trim(std::string value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}
[[nodiscard]] inline std::string trim_boundary(std::string value) {
    while (!value.empty()) {
        const auto character = value.back();
        if (character != '_' && character != '-' && character != '.' && character != ' ') {
            break;
        }
        value.pop_back();
    }
    return value;
}
[[nodiscard]] inline bool viable_ied_name(const std::string_view value) {
    return std::count_if(value.begin(), value.end(), [](const char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0;
    }) >= 3;
}
inline void append_unique_ci(
    std::vector<std::string>& values,
    const std::string& value) {
    if (std::none_of(values.begin(), values.end(), [&value](const auto& existing) {
            return same(existing, value);
        })) {
        values.push_back(value);
    }
}
[[nodiscard]] inline bool try_extract_known_ld_prefix(
    const std::string_view domain,
    std::string& candidate) {
    static constexpr std::array<std::string_view, 14> stems{
        "PROT", "CTRL", "MEAS", "PQM", "MET", "ANN", "BCU",
        "SYS", "COM", "RLY", "BAY", "DR", "LD", "MU"};

    const auto cleaned = trim(std::string{domain});
    std::size_t suffix_start = cleaned.size();
    while (suffix_start > 0U &&
           std::isdigit(static_cast<unsigned char>(cleaned[suffix_start - 1U])) != 0) {
        --suffix_start;
    }
    const auto without_index = cleaned.substr(0U, suffix_start);
    for (const auto stem : stems) {
        if (!ends_with_ci(without_index, stem) || without_index.size() <= stem.size()) {
            continue;
        }
        auto prefix = trim_boundary(
            without_index.substr(0U, without_index.size() - stem.size()));
        if (!viable_ied_name(prefix)) {
            continue;
        }
        candidate = std::move(prefix);
        return true;
    }
    return false;
}
[[nodiscard]] inline std::string infer_common_prefix(
    const std::vector<std::string>& domains) {
    if (domains.size() < 2U) {
        return {};
    }

    std::string prefix = domains.front();
    for (std::size_t index = 1U; index < domains.size(); ++index) {
        const auto& domain = domains[index];
        std::size_t length = 0U;
        while (length < prefix.size() && length < domain.size() &&
               std::toupper(static_cast<unsigned char>(prefix[length])) ==
                   std::toupper(static_cast<unsigned char>(domain[length]))) {
            ++length;
        }
        prefix.resize(length);
        if (prefix.empty()) {
            return {};
        }
    }

    prefix = trim_boundary(std::move(prefix));
    static constexpr std::array<std::string_view, 14> stems{
        "PROT", "CTRL", "MEAS", "PQM", "MET", "ANN", "BCU",
        "SYS", "COM", "RLY", "BAY", "DR", "LD", "MU"};
    for (const auto stem : stems) {
        if (ends_with_ci(prefix, stem) && prefix.size() > stem.size()) {
            auto without_stem = trim_boundary(
                prefix.substr(0U, prefix.size() - stem.size()));
            if (viable_ied_name(without_stem)) {
                prefix = std::move(without_stem);
            }
            break;
        }
    }
    return viable_ied_name(prefix) ? prefix : std::string{};
}
[[nodiscard]] inline MmsLiveIedIdentity resolve_ied_identity(
    const std::vector<std::string>& input_domains,
    const MmsLiveModelBuildOptions& options,
    const MmsEndpoint& endpoint) {
    std::vector<std::string> domains;
    for (const auto& raw_domain : input_domains) {
        const auto domain = trim(raw_domain);
        if (!domain.empty()) {
            append_unique_ci(domains, domain);
        }
    }
    std::sort(domains.begin(), domains.end(), [](const auto& left, const auto& right) {
        return lower(left) < lower(right);
    });

    const auto make = [&](std::string name,
                          std::string source,
                          const MmsLiveModelConfidence confidence_value,
                          const bool ambiguous,
                          std::vector<std::string> candidates,
                          std::vector<std::string> evidence) {
        MmsLiveIedIdentity identity;
        identity.ied_name = trim(std::move(name));
        identity.source = std::move(source);
        identity.confidence = confidence_value;
        identity.ambiguous = ambiguous;
        identity.candidate_names = std::move(candidates);
        identity.evidence = std::move(evidence);
        for (const auto& domain : domains) {
            std::string alias = domain;
            if (starts_with_ci(domain, identity.ied_name) &&
                domain.size() > identity.ied_name.size()) {
                alias = domain.substr(identity.ied_name.size());
            }
            if (alias.empty()) {
                alias = domain;
            }
            identity.logical_device_aliases.emplace(domain, std::move(alias));
        }
        return identity;
    };

    const auto fallback = [&]() {
        if (!trim(options.fallback_ied_name).empty()) {
            return trim(options.fallback_ied_name);
        }
        const auto host = trim(endpoint.host);
        return host.empty() ? std::string{"DISCOVERED_IED"} : host;
    };

    if (const auto explicit_name = trim(options.explicit_ied_name);
        !explicit_name.empty()) {
        return make(
            explicit_name,
            "ExplicitOverride",
            MmsLiveModelConfidence::exact,
            false,
            {explicit_name},
            {"IED name was supplied explicitly as '" + explicit_name + "'."});
    }

    struct SuffixMatch final {
        std::string domain;
        std::string candidate;
    };
    std::vector<SuffixMatch> suffix_matches;
    std::vector<std::string> distinct_candidates;
    for (const auto& domain : domains) {
        std::string candidate;
        if (try_extract_known_ld_prefix(domain, candidate)) {
            suffix_matches.push_back({domain, candidate});
            append_unique_ci(distinct_candidates, candidate);
        }
    }
    std::sort(distinct_candidates.begin(), distinct_candidates.end(),
              [](const auto& left, const auto& right) {
                  return lower(left) < lower(right);
              });

    if (!domains.empty() && suffix_matches.size() == domains.size()) {
        if (distinct_candidates.size() == 1U) {
            const auto& name = distinct_candidates.front();
            std::vector<std::string> evidence;
            for (const auto& match : suffix_matches) {
                evidence.push_back(
                    "MMS domain '" + match.domain +
                    "' matched the logical-device suffix pattern for IED '" + name + "'.");
            }
            return make(
                name,
                "MmsDomainKnownLogicalDeviceSuffix",
                domains.size() > 1U
                    ? MmsLiveModelConfidence::high
                    : MmsLiveModelConfidence::medium,
                false,
                distinct_candidates,
                std::move(evidence));
        }
        if (distinct_candidates.size() > 1U) {
            std::ostringstream message;
            message << "MMS domains produced conflicting IED-name candidates: ";
            for (std::size_t index = 0U; index < distinct_candidates.size(); ++index) {
                if (index) message << ", ";
                message << distinct_candidates[index];
            }
            message << '.';
            return make(
                fallback(),
                "MmsDomainAmbiguous",
                MmsLiveModelConfidence::low,
                true,
                distinct_candidates,
                {message.str()});
        }
    }

    if (const auto common_prefix = infer_common_prefix(domains);
        !common_prefix.empty()) {
        return make(
            common_prefix,
            "MmsDomainCommonPrefix",
            domains.size() >= 3U
                ? MmsLiveModelConfidence::high
                : MmsLiveModelConfidence::medium,
            false,
            {common_prefix},
            {"IED name '" + common_prefix +
             "' was derived from the common prefix of " +
             std::to_string(domains.size()) + " MMS domain(s)."});
    }

    if (distinct_candidates.size() == 1U) {
        const auto& candidate = distinct_candidates.front();
        const bool shared_prefix = suffix_matches.size() >= 2U &&
            std::all_of(domains.begin(), domains.end(), [&candidate](const auto& domain) {
                return starts_with_ci(domain, candidate);
            });
        if (domains.size() == 1U || shared_prefix) {
            std::vector<std::string> evidence;
            for (const auto& match : suffix_matches) {
                evidence.push_back(
                    "MMS domain '" + match.domain +
                    "' matched the logical-device suffix pattern for IED '" + candidate + "'.");
            }
            return make(
                candidate,
                "MmsDomainKnownLogicalDeviceSuffix",
                domains.size() == 1U
                    ? MmsLiveModelConfidence::medium
                    : MmsLiveModelConfidence::high,
                false,
                distinct_candidates,
                std::move(evidence));
        }
    }

    if (distinct_candidates.size() > 1U) {
        std::ostringstream message;
        message << "MMS domains produced conflicting IED-name candidates: ";
        for (std::size_t index = 0U; index < distinct_candidates.size(); ++index) {
            if (index) message << ", ";
            message << distinct_candidates[index];
        }
        message << '.';
        return make(
            fallback(),
            "MmsDomainAmbiguous",
            MmsLiveModelConfidence::low,
            true,
            distinct_candidates,
            {message.str()});
    }

    return make(
        fallback(),
        "HostFallback",
        MmsLiveModelConfidence::low,
        false,
        {},
        {"No safe IED-name candidate could be derived from the live MMS domains."});
}
[[nodiscard]] inline std::vector<MmsLivePointReference> collect_points(
    const MmsLiveDiscoveryResult& discovery) {
    std::map<std::string, MmsLivePointReference, std::less<>> unique;
    const auto add = [&](std::optional<MmsLivePointReference> point) {
        if (point) unique.emplace(key(point->mms_reference()), std::move(*point));
    };
    for (const auto& [domain, variables] : discovery.names.domain_variables) {
        for (const auto& variable : variables) add(MmsLiveReferenceParser::parse_variable(domain, variable));
    }
    for (const auto& evidence : discovery.data_set_directories) {
        if (!evidence.success()) continue;
        for (const auto& member : evidence.directory->members) {
            add(MmsLiveReferenceParser::parse_variable(
                member.object_name.domain, member.object_name.item,
                "GetNamedVariableListAttributes", member.confidence));
        }
    }
    for (const auto& control : discovery.report_inventory.report_controls) {
        add(MmsLiveReferenceParser::parse_variable(
            control.domain,
            control.logical_node + "$" + (control.buffered ? "BR" : "RP") +
                "$" + control.name + "$RptEna",
            "ReportControlInventory", 100U));
    }
    std::vector<MmsLivePointReference> result;
    for (auto& [_, point] : unique) result.push_back(std::move(point));
    return result;
}
[[nodiscard]] inline std::map<std::string, const MmsLiveDataAttribute*, std::less<>> attributes(
    const MmsLiveModelDocument& document) {
    std::map<std::string, const MmsLiveDataAttribute*, std::less<>> result;
    for (const auto& ld : document.logical_devices)
        for (const auto& ln : ld.logical_nodes)
            for (const auto& object : ln.data_objects)
                for (const auto& attribute : object.attributes)
                    result.emplace(key(attribute.object_reference), &attribute);
    return result;
}
} // namespace live_model_detail

inline MmsLiveModelDocument MmsLiveModelBuilder::build(
    const MmsLiveDiscoveryResult& discovery,
    const MmsLiveModelBuildOptions& options) {
    using namespace live_model_detail;
    MmsLiveModelDocument document;
    document.endpoint = discovery.endpoint;
    document.access_point_name = options.access_point_name.empty() ? "AP1" : options.access_point_name;
    std::vector<std::string> domains;
    for (const auto& [domain, _] : discovery.names.domain_variables) domains.push_back(domain);
    document.identity = resolve_ied_identity(domains, options, discovery.endpoint);

    using AttributeMap = std::map<std::string, MmsLiveDataAttribute, std::less<>>;
    using ObjectMap = std::map<std::string, AttributeMap, std::less<>>;
    using NodeMap = std::map<std::string, ObjectMap, std::less<>>;
    std::map<std::string, NodeMap, std::less<>> hierarchy;
    for (const auto& point : collect_points(discovery)) {
        const auto object_name = MmsLiveReferenceParser::top_data_object_name(point.data_object_path);