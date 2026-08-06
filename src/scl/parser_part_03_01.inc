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
