    if (magnitude && quality && timestamp) return {"MV", 0.82};
    if (st_val && quality && timestamp) return {"SPS", 0.78};
    if (st_val) return {"SPS", 0.60};
    return {"", 0.0};
}
[[nodiscard]] inline std::string infer_ied_name(
    const std::vector<std::string>& domains,
    const MmsLiveModelBuildOptions& options,
    const MmsEndpoint& endpoint) {
    if (!options.explicit_ied_name.empty()) return options.explicit_ied_name;
    static constexpr std::array<std::string_view, 12> suffixes{
        "LD0", "LD1", "CTRL", "PROT", "MEAS", "CFG", "SYS", "BAY", "MU", "P1", "P2", "DR"};
    std::vector<std::string> candidates;
    for (const auto& domain : domains) {
        for (const auto suffix : suffixes) {
            if (domain.size() > suffix.size() &&
                same(domain.substr(domain.size() - suffix.size()), suffix)) {
                candidates.push_back(domain.substr(0, domain.size() - suffix.size()));
                break;
            }
        }
    }
    if (!candidates.empty() && std::all_of(candidates.begin(), candidates.end(),
        [&](const auto& value) { return same(value, candidates.front()); })) {
        return candidates.front();
    }
    if (!options.fallback_ied_name.empty()) return options.fallback_ied_name;
    return endpoint.host.empty() ? "DISCOVERED_IED" : endpoint.host;
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
    std::sort(domains.begin(), domains.end());
    document.identity.ied_name = infer_ied_name(domains, options, discovery.endpoint);
    document.identity.source = options.explicit_ied_name.empty() ? "MmsDomainInference" : "ExplicitOverride";
    document.identity.confidence = options.explicit_ied_name.empty()
        ? MmsLiveModelConfidence::medium : MmsLiveModelConfidence::exact;
    for (const auto& domain : domains) {
        std::string alias = domain;
        if (domain.starts_with(document.identity.ied_name) && domain.size() > document.identity.ied_name.size())
            alias = domain.substr(document.identity.ied_name.size());
        document.identity.logical_device_aliases.emplace(domain, alias);
    }

    using AttributeMap = std::map<std::string, MmsLiveDataAttribute, std::less<>>;
    using ObjectMap = std::map<std::string, AttributeMap, std::less<>>;
    using NodeMap = std::map<std::string, ObjectMap, std::less<>>;
    std::map<std::string, NodeMap, std::less<>> hierarchy;
    for (const auto& point : collect_points(discovery)) {
        const auto object_name = MmsLiveReferenceParser::top_data_object_name(point.data_object_path);
