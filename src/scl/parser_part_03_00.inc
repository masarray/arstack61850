
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
