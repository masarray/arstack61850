    [[nodiscard]] std::string decode_entities(const std::string_view raw) const {
        std::string output;
        output.reserve(raw.size());
        std::size_t offset{};
        while (offset < raw.size()) {
            if (raw[offset] != '&') {
                output.push_back(raw[offset++]);
                continue;
            }

            const auto semicolon = raw.find(';', offset + 1U);
            if (semicolon == std::string_view::npos) {
                throw std::runtime_error("Unterminated XML entity reference.");
            }
            const auto entity = raw.substr(offset + 1U, semicolon - offset - 1U);
            if (entity == "amp") {
                output.push_back('&');
            } else if (entity == "lt") {
                output.push_back('<');
            } else if (entity == "gt") {
                output.push_back('>');
            } else if (entity == "quot") {
                output.push_back('"');
            } else if (entity == "apos") {
                output.push_back('\'');
            } else if (entity.starts_with("#x") || entity.starts_with("#X")) {
                append_utf8(output, parse_code_point(entity.substr(2U), 16));
            } else if (entity.starts_with('#')) {
                append_utf8(output, parse_code_point(entity.substr(1U), 10));
            } else {
                throw std::runtime_error(
                    "Named XML entities other than the five predefined entities are prohibited.");
            }
            offset = semicolon + 1U;
        }
        return output;
    }

    [[nodiscard]] std::string parse_quoted_value() {
        if (position_ >= input_.size() ||
            (input_[position_] != '"' && input_[position_] != '\'')) {
            fail("Expected quoted XML attribute value");
        }
        const char quote = input_[position_++];
        const auto start = position_;
        while (position_ < input_.size() && input_[position_] != quote) {
            if (input_[position_] == '<') {
                fail("XML attribute value contains '<'");
            }
            ++position_;
        }
        if (position_ >= input_.size()) {
            fail("Unterminated XML attribute value");
        }
        const auto raw = input_.substr(start, position_ - start);
        ++position_;
        return decode_entities(raw);
    }

    [[nodiscard]] XmlNode parse_element(const std::size_t depth) {
        if (depth > max_xml_depth) {
            fail("XML nesting exceeds the supported depth");
        }
        if (++node_count_ > max_xml_nodes) {
            fail("XML document exceeds the supported node count");
        }
        if (position_ >= input_.size() || input_[position_] != '<' || starts_with("</")) {
            fail("Expected XML start element");
        }
        ++position_;

        if (position_ < input_.size() && input_[position_] == '!') {
            fail("DTD, entity declarations, and unsupported markup are prohibited");
        }

        XmlNode node;
        node.name = parse_name();

        while (true) {
            skip_whitespace();
            if (starts_with("/>")) {
                position_ += 2U;
                return node;
            }
            if (position_ < input_.size() && input_[position_] == '>') {
                ++position_;
                break;
            }
            if (position_ >= input_.size()) {
                fail("Unterminated XML start element");
            }
            if (node.attributes.size() >= max_attributes_per_element) {
                fail("XML element exceeds the supported attribute count");
            }

            XmlAttribute attribute;
            attribute.name = parse_name();
            skip_whitespace();
            if (position_ >= input_.size() || input_[position_] != '=') {
                fail("Expected '=' after XML attribute name");
            }
            ++position_;
            skip_whitespace();
            attribute.value = parse_quoted_value();

            const auto duplicate = std::find_if(
                node.attributes.begin(),
                node.attributes.end(),
                [&attribute](const XmlAttribute& existing) {
                    return existing.name == attribute.name;
                });
            if (duplicate != node.attributes.end()) {
                fail("Duplicate XML attribute '" + attribute.name + "'");
            }
            node.attributes.push_back(std::move(attribute));
        }

        while (true) {
            if (position_ >= input_.size()) {
                fail("Unterminated XML element '" + node.name + "'");
            }
            if (starts_with("</")) {
                position_ += 2U;
                const auto closing_name = parse_name();
                skip_whitespace();
                if (position_ >= input_.size() || input_[position_] != '>') {
                    fail("Expected '>' after XML closing element");
                }
                ++position_;
                if (closing_name != node.name) {
                    fail("Mismatched XML closing element '" + closing_name + "' for '" + node.name + "'");
                }
                return node;
            }
            if (starts_with("<!--")) {
                skip_comment();
                continue;
            }
            if (starts_with("<?")) {
                skip_processing_instruction();
                continue;
            }
            if (starts_with("<![CDATA[")) {
                const auto end = input_.find("]]>", position_ + 9U);
                if (end == std::string_view::npos) {
                    fail("Unterminated CDATA section");
                }
                node.text.append(input_.substr(position_ + 9U, end - position_ - 9U));
                position_ = end + 3U;
                continue;
            }
            if (starts_with("<!DOCTYPE") || starts_with("<!ENTITY") || starts_with("<!")) {
                fail("DTD, entity declarations, and unsupported markup are prohibited");
            }
            if (input_[position_] == '<') {
                node.children.push_back(parse_element(depth + 1U));
                continue;
            }

            const auto end = input_.find('<', position_);
            const auto text_end = end == std::string_view::npos ? input_.size() : end;
            node.text += decode_entities(input_.substr(position_, text_end - position_));
            position_ = text_end;
        }
    }
};

bool is(const XmlNode& node, const std::string_view name) {
    return local_name(node.name) == name;
}

std::string attribute(const XmlNode* node, const std::string_view name) {
    if (node == nullptr) {
        return {};
    }
    const auto exact = std::find_if(
        node->attributes.begin(),
        node->attributes.end(),
        [name](const XmlAttribute& item) { return item.name == name; });
    if (exact != node->attributes.end()) {
        return trim_copy(exact->value);
    }
    const auto by_local_name = std::find_if(
        node->attributes.begin(),
        node->attributes.end(),
        [name](const XmlAttribute& item) { return local_name(item.name) == name; });
    return by_local_name == node->attributes.end() ? std::string{} : trim_copy(by_local_name->value);
}

const XmlNode* first_child(const XmlNode& node, const std::string_view name) {
    const auto match = std::find_if(
        node.children.begin(),
        node.children.end(),
        [name](const XmlNode& child) { return is(child, name); });
    return match == node.children.end() ? nullptr : &*match;
}

std::vector<const XmlNode*> direct_children(const XmlNode& node, const std::string_view name) {
    std::vector<const XmlNode*> result;
    for (const auto& child : node.children) {
        if (is(child, name)) {
            result.push_back(&child);
        }
    }
    return result;
}

void collect_descendants(
    const XmlNode& node,
    const std::string_view name,
    std::vector<const XmlNode*>& result) {
    for (const auto& child : node.children) {
        if (is(child, name)) {
            result.push_back(&child);
        }
        collect_descendants(child, name, result);
    }
}

std::vector<const XmlNode*> descendants(const XmlNode& node, const std::string_view name) {
    std::vector<const XmlNode*> result;
    collect_descendants(node, name, result);
    return result;
}

