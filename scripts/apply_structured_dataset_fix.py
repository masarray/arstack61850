#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


# 1) Preserve configured FCDA membership separately from the leaf projection
# consumed by process-bus payload compilers.
replace_once(
    "include/ariec61850/scl/model.hpp",
    """    std::string reference;\n    std::vector<SclDataSetEntry> entries;\n\n    friend bool operator==(const SclDataSet&, const SclDataSet&) = default;\n""",
    """    std::string reference;\n\n    // Canonical SCL FCDA membership. A whole-DataObject FCDA intentionally keeps\n    // da_name empty and therefore remains one MMS DataSet member.\n    std::vector<SclDataSetEntry> entries;\n\n    // Ordered leaf projection used by payload-oriented profiles such as SV and\n    // the current GOOSE publisher path. This view may contain several entries\n    // for one configured whole-DataObject FCDA.\n    std::vector<SclDataSetEntry> expanded_entries;\n\n    friend bool operator==(const SclDataSet&, const SclDataSet&) = default;\n""",
)

replace_once(
    "src/scl/parser_part_04.inc",
    """    if (!type.resolved && !signal_reference.empty()) {\n        warnings.push_back(\"Type unresolved for \" + signal_reference + \".\");\n    }\n""",
    """    // A configured FCDA may intentionally name the whole DataObject and leave\n    // daName empty. That is a valid IEC 61850 DataSet member, not an unresolved\n    // scalar leaf. Payload-oriented users can expand it separately below.\n    if (!type.resolved && !signal_reference.empty() && !da_name.empty()) {\n        warnings.push_back(\"Type unresolved for \" + signal_reference + \".\");\n    }\n""",
)

replace_once(
    "src/scl/parser_part_04.inc",
    """                    std::size_t entry_index = 1U;\n                    for (const auto* fcda : direct_children(*data_set_node, \"FCDA\")) {\n                        auto entries = build_data_set_entries(\n                            ied_name,\n                            *fcda,\n                            entry_index,\n                            type_index,\n                            document.warnings);\n                        entry_index += entries.size();\n                        for (auto& entry : entries) {\n                            data_set.entries.push_back(std::move(entry));\n                        }\n                    }\n""",
    """                    std::size_t configured_entry_index = 1U;\n                    std::size_t expanded_entry_index = 1U;\n                    for (const auto* fcda : direct_children(*data_set_node, \"FCDA\")) {\n                        const auto explicit_da_name = attribute(fcda, \"daName\");\n                        if (!explicit_da_name.empty()) {\n                            auto configured = build_data_set_entry(\n                                ied_name,\n                                *fcda,\n                                configured_entry_index++,\n                                type_index,\n                                document.warnings);\n                            auto expanded = configured;\n                            expanded.index = expanded_entry_index++;\n                            data_set.entries.push_back(std::move(configured));\n                            data_set.expanded_entries.push_back(std::move(expanded));\n                            continue;\n                        }\n\n                        data_set.entries.push_back(build_data_set_entry(\n                            ied_name,\n                            *fcda,\n                            configured_entry_index++,\n                            type_index,\n                            document.warnings));\n                        auto expanded = build_data_set_entries(\n                            ied_name,\n                            *fcda,\n                            expanded_entry_index,\n                            type_index,\n                            document.warnings);\n                        expanded_entry_index += expanded.size();\n                        for (auto& entry : expanded) {\n                            data_set.expanded_entries.push_back(std::move(entry));\n                        }\n                    }\n""",
)

for path in ("src/scl/parser_part_05_01.inc", "src/scl/parser_part_05_02.inc"):
    replace_once(
        path,
        """                    if (data_set != nullptr) {\n                        stream.entries = data_set->entries;\n                    }\n""",
        """                    if (data_set != nullptr) {\n                        stream.entries = data_set->expanded_entries.empty()\n                            ? data_set->entries\n                            : data_set->expanded_entries;\n                    }\n""",
    )

# 2) Strengthen the existing structured-SV fixture so CI proves both semantics:
# canonical DataSet membership stays whole-DO while SV remains leaf-expanded.
replace_once(
    "tests/test_scl.cpp",
    """    CHECK(document.sampled_values_streams.size() == 1U);\n    CHECK(document.data_sets.size() == 1U);\n    CHECK(document.warnings.empty());\n\n    const auto& stream = document.sampled_values_streams.front();\n""",
    """    CHECK(document.sampled_values_streams.size() == 1U);\n    CHECK(document.data_sets.size() == 1U);\n    CHECK(document.warnings.empty());\n\n    const auto& configured_data_set = document.data_sets.front();\n    CHECK(configured_data_set.entries.size() == 8U);\n    CHECK(configured_data_set.expanded_entries.size() == 16U);\n    CHECK(std::all_of(\n        configured_data_set.entries.begin(),\n        configured_data_set.entries.end(),\n        [](const SclDataSetEntry& entry) { return entry.da_name.empty(); }));\n\n    const auto& stream = document.sampled_values_streams.front();\n""",
)

# 3) The host MMS server already owns a hierarchy tree for each logical node.
# Expose configured intermediate nodes (whole DO / structured DA) as real MMS
# objects when a DataSet references them, and keep their encoded values live.
replace_once(
    "tools/static_ied_server.cpp",
    """struct ManifestValue final {\n    std::string domain;\n    std::string item;\n    std::string raw_type;\n    std::string normalized_type;\n    std::string text;\n    mms::MmsTypeSpecification type;\n    std::optional<mms::MmsDataValue> data;\n    std::vector<std::uint8_t> type_specification;\n    std::vector<std::uint8_t> encoded;\n    bool root{};\n};\n\nstruct ManifestTypeNode final {\n""",
    """struct ManifestTypeNode;\n\nstruct ManifestValue final {\n    std::string domain;\n    std::string item;\n    std::string raw_type;\n    std::string normalized_type;\n    std::string text;\n    mms::MmsTypeSpecification type;\n    std::optional<mms::MmsDataValue> data;\n    std::vector<std::uint8_t> type_specification;\n    std::vector<std::uint8_t> encoded;\n    const ManifestTypeNode* structured_node{};\n    bool root{};\n};\n\nstruct ManifestTypeNode final {\n""",
)

replace_once(
    "tools/static_ied_server.cpp",
    """        auto& root = model.values[value_index];\n        root.data = node_data(tree, model);\n        root.encoded = mms::MmsDataCodec::encode(*root.data);\n    }\n}\n\n[[nodiscard]] std::uint64_t manifest_revision""",
    """        auto& root = model.values[value_index];\n        root.data = node_data(tree, model);\n        root.encoded = mms::MmsDataCodec::encode(*root.data);\n    }\n    for (auto& value : model.values) {\n        if (value.structured_node == nullptr) continue;\n        value.data = node_data(*value.structured_node, model);\n        value.encoded = mms::MmsDataCodec::encode(*value.data);\n    }\n}\n\n[[nodiscard]] std::uint64_t manifest_revision""",
)

replace_once(
    "tools/static_ied_server.cpp",
    """    result.append(item);\n    return result;\n}\n\n[[nodiscard]] ManifestModel load_manifest_model(\n""",
    """    result.append(item);\n    return result;\n}\n\n[[nodiscard]] const ManifestTypeNode* find_manifest_node(\n    const ManifestModel& model,\n    const std::unordered_map<std::string, std::size_t>& root_indices,\n    const std::string_view domain,\n    const std::string_view item) {\n    const auto parts = split_fields(item, '$');\n    if (parts.size() < 2U) return nullptr;\n    const auto root = root_indices.find(object_key(domain, parts.front()));\n    if (root == root_indices.end() || root->second >= model.root_trees.size()) {\n        return nullptr;\n    }\n    const auto* node = &model.root_trees[root->second];\n    for (std::size_t part = 1U; part < parts.size(); ++part) {\n        const auto child = node->children.find(parts[part]);\n        if (child == node->children.end()) return nullptr;\n        node = &child->second;\n    }\n    return node;\n}\n\n[[nodiscard]] ManifestModel load_manifest_model(\n""",
)

replace_once(
    "tools/static_ied_server.cpp",
    """    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, std::string>>>\n        grouped_members;\n    for (const auto& member : parsed_members) {\n        if (!model.value_indices.contains(object_key(member.member_domain, member.member_item))) {\n            continue;\n        }\n        grouped_members[{member.domain, member.item}].emplace_back(\n            member.member_domain, member.member_item);\n    }\n""",
    """    const auto ensure_data_set_member_object = [&](const ParsedDataSetMember& member) {\n        const auto key = object_key(member.member_domain, member.member_item);\n        if (model.value_indices.contains(key)) return true;\n        if (model.values.size() >= mms::MmsStaticObjectTable::maximum_objects) return false;\n\n        const auto* node = find_manifest_node(\n            model, root_indices, member.member_domain, member.member_item);\n        if (node == nullptr || node->children.empty()) return false;\n\n        const auto parts = split_fields(member.member_item, '$');\n        ManifestValue value;\n        value.domain = member.member_domain;\n        value.item = member.member_item;\n        value.structured_node = node;\n        value.type = node_type(*node, model, parts.empty() ? std::string{} : parts.back());\n        value.data = node_data(*node, model);\n        value.type_specification = mms::MmsServiceCodec::encode_type_specification(value.type);\n        value.encoded = mms::MmsDataCodec::encode(*value.data);\n\n        const auto value_index = model.values.size();\n        model.values.push_back(std::move(value));\n        model.value_indices.emplace(key, value_index);\n        auto& stored = model.values.back();\n        model.objects.push_back(mms::MmsStaticObjectEntry{\n            stored.domain,\n            stored.item,\n            stored.type_specification,\n            read_manifest_value,\n            &stored});\n        return true;\n    };\n\n    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, std::string>>>\n        grouped_members;\n    for (const auto& member : parsed_members) {\n        if (!ensure_data_set_member_object(member)) continue;\n        grouped_members[{member.domain, member.item}].emplace_back(\n            member.member_domain, member.member_item);\n    }\n""",
)

replace_once(
    "tools/static_ied_server.cpp",
    """                const auto& member = brcb->data_set->members[member_index];\n                if (member.domain != value.domain || member.item != value.item) continue;\n                const auto status = brcb->reports->notify(member_index, reason, now_ms);\n""",
    """                const auto& member = brcb->data_set->members[member_index];\n                const auto member_matches_value =\n                    member.domain == value.domain &&\n                    (member.item == value.item ||\n                     (value.item.size() > member.item.size() &&\n                      value.item.compare(0U, member.item.size(), member.item) == 0 &&\n                      value.item[member.item.size()] == '$'));\n                if (!member_matches_value) continue;\n                const auto status = brcb->reports->notify(member_index, reason, now_ms);\n""",
)

print("structured DataSet FCDA patch applied")
