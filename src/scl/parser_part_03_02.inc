        std::unordered_map<std::string, const XmlNode*>& index,
        const std::string& key,
        const XmlNode* value) {
        if (!key.empty()) {
            index.try_emplace(normalized_key(key), value);
        }
    }

    static const XmlNode* find(
        const std::unordered_map<std::string, const XmlNode*>& index,
        const std::string& key) {
        const auto match = index.find(normalized_key(key));
        return match == index.end() ? nullptr : match->second;
    }

    static Resolution unresolved(const std::string& functional_constraint) {
        return {false, functional_constraint, {}, {}, {}, {}};
    }

    const XmlNode* find_logical_node(
        const std::string& ied_name,
        const std::string& ld_inst,
        const std::string& prefix,
        const std::string& ln_class,
        const std::string& ln_inst) const {
        const auto* ied = find(ieds_, ied_name);
        if (ied == nullptr) {
            return nullptr;
        }

        const XmlNode* logical_device{};
        for (const auto* candidate : descendants(*ied, "LDevice")) {
            if (same(attribute(candidate, "inst"), ld_inst)) {
                logical_device = candidate;
                break;
            }
        }
        if (logical_device == nullptr) {
            return nullptr;
        }

        if (same(ln_class, "LLN0")) {
            return first_child(*logical_device, "LN0");
        }

        for (const auto* logical_node : direct_children(*logical_device, "LN")) {
            if (same(attribute(logical_node, "prefix"), prefix) &&
                same(attribute(logical_node, "lnClass"), ln_class) &&
                same(attribute(logical_node, "inst"), ln_inst)) {
                return logical_node;
            }
        }
        return nullptr;
    }
};

SclDataSetEntry build_data_set_entry(
    const std::string& fallback_ied_name,
    const XmlNode& fcda,
    const std::size_t index,
    const SclTypeIndex& type_index,
    std::vector<std::string>& warnings) {
    auto ied_name = attribute(&fcda, "iedName");
    if (ied_name.empty()) {
        ied_name = fallback_ied_name;
    }
    const auto ld_inst = attribute(&fcda, "ldInst");
    const auto prefix = attribute(&fcda, "prefix");
    const auto ln_class = attribute(&fcda, "lnClass");
    const auto ln_inst = attribute(&fcda, "lnInst");
    const auto do_name = attribute(&fcda, "doName");
    const auto da_name = attribute(&fcda, "daName");
    const auto functional_constraint = attribute(&fcda, "fc");
    const auto type = type_index.resolve(
        ied_name,
