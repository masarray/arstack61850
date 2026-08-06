
    [[nodiscard]] Resolution resolve(
        const std::string& ied_name,
        const std::string& ld_inst,
        const std::string& prefix,
        const std::string& ln_class,
        const std::string& ln_inst,
        const std::string& do_name,
        const std::string& da_name,
        const std::string& functional_constraint) const {
        const auto* logical_node = find_logical_node(
            ied_name,
            ld_inst,
            prefix,
            ln_class,
            ln_inst);
        if (logical_node == nullptr) {
            return unresolved(functional_constraint);
        }

        const auto logical_node_type_id = attribute(logical_node, "lnType");
        const auto* logical_node_type = find(logical_node_types_, logical_node_type_id);
        if (logical_node_type == nullptr) {
            return unresolved(functional_constraint);
        }

        const auto do_segments = split_name(do_name);
        if (do_segments.empty()) {
            return unresolved(functional_constraint);
        }

        const XmlNode* data_object{};
        for (const auto* candidate : direct_children(*logical_node_type, "DO")) {
            if (same(attribute(candidate, "name"), do_segments.front())) {
                data_object = candidate;
                break;
            }
        }
        if (data_object == nullptr) {
            return unresolved(functional_constraint);
        }

        auto current_type_id = attribute(data_object, "type");
        const XmlNode* data_object_type = find(data_object_types_, current_type_id);
        if (data_object_type == nullptr) {
            return unresolved(functional_constraint);
        }

        for (std::size_t index = 1U; index < do_segments.size(); ++index) {
            const XmlNode* structured_data_object{};
            for (const auto* candidate : direct_children(*data_object_type, "SDO")) {
                if (same(attribute(candidate, "name"), do_segments[index])) {
                    structured_data_object = candidate;
                    break;
                }
            }
            if (structured_data_object == nullptr) {
                return {
                    false,
                    functional_constraint,
                    attribute(data_object_type, "cdc"),
                    {},
                    current_type_id,
                    {},
                };
            }
            current_type_id = attribute(structured_data_object, "type");
            data_object_type = find(data_object_types_, current_type_id);
            if (data_object_type == nullptr) {
                return {
                    false,
                    functional_constraint,
                    {},
                    {},
                    current_type_id,
                    {},
                };
            }
        }

        const auto cdc = attribute(data_object_type, "cdc");
        const auto da_segments = split_name(da_name);
        if (da_segments.empty()) {
            return {true, functional_constraint, cdc, {}, current_type_id, {}};
        }

        const XmlNode* current_container = data_object_type;
        std::string basic_type;
        std::string enum_type;
        auto resolved_fc = functional_constraint;

        for (const auto& segment : da_segments) {
            const XmlNode* child{};
            for (const auto& candidate : current_container->children) {
                if ((is(candidate, "DA") || is(candidate, "BDA")) &&
                    same(attribute(&candidate, "name"), segment)) {
                    child = &candidate;
                    break;
                }
            }
            if (child == nullptr) {
                return {false, resolved_fc, cdc, basic_type, current_type_id, enum_type};
            }

            basic_type = attribute(child, "bType");
            if (resolved_fc.empty()) {
                resolved_fc = attribute(child, "fc");
            }
            const auto child_type = attribute(child, "type");
            if (same(basic_type, "Struct")) {
                const auto* attribute_type = find(data_attribute_types_, child_type);
                if (attribute_type == nullptr) {
                    return {false, resolved_fc, cdc, basic_type, child_type, enum_type};
                }
                current_container = attribute_type;
                current_type_id = child_type;
                continue;
            }
            if (same(basic_type, "Enum") &&
                enum_type_ids_.contains(normalized_key(child_type))) {
                enum_type = child_type;
            }
            current_type_id = child_type;
        }

        return {true, resolved_fc, cdc, basic_type, current_type_id, enum_type};
    }

private:
    std::unordered_map<std::string, const XmlNode*> ieds_;
    std::unordered_map<std::string, const XmlNode*> logical_node_types_;
    std::unordered_map<std::string, const XmlNode*> data_object_types_;
    std::unordered_map<std::string, const XmlNode*> data_attribute_types_;
    std::unordered_set<std::string> enum_type_ids_;

    static void add_first(
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
        for (const auto* candidate : descendants**ied, "LDevice")) {
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
        ld_inst,
        prefix,
        ln_class,
        ln_inst,
        do_name,
        da_name,
        functional_constraint);
    const auto signal_reference = build_signal_reference(
        ied_name,
        ld_inst,
        prefix,
        ln_class,
        ln_inst,
        do_name,
