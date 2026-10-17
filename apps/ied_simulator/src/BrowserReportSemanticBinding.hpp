// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/mms/reporting.hpp"
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Native port of ARIEC61850 MmsSemanticReportValueProjector's fail-closed
// invariants. Read-only, no additional protocol stack or MMS association.
namespace arstack::iedsim {
namespace semantic_mms = ar::iec61850::mms;
struct VerifiedReportLeaf final {
    std::string reference;
    const semantic_mms::MmsDataValue* value{};
};
inline bool semanticInside(const std::string_view leaf,
                           const std::string_view root) {
    return leaf.size() > root.size() && leaf.substr(0,root.size()) == root &&
           leaf[root.size()] == '.';
}
inline semantic_mms::MmsDataKind semanticExpectedKind(const std::string_view basic) {
    using K = semantic_mms::MmsDataKind;
    if (basic == "BOOLEAN") return K::boolean;
    if (basic == "Quality" || basic == "QUALITY") return K::bit_string;
    if (basic == "Timestamp" || basic == "TIMESTAMP") return K::utc_time;
    if (basic == "Enum" || basic == "ENUM" || basic == "INT32" ||
        basic == "INT8" || basic == "INT16") return K::integer;
    if (basic == "FLOAT32" || basic == "FLOAT64") return K::floating_point;
    return K::unknown;
}
inline bool semanticKindCompatible(const semantic_mms::MmsDataValue& value,
                                    const std::string_view basic) {
    using K = semantic_mms::MmsDataKind;
    if (basic == "BOOLEAN") return value.kind() == K::boolean;
    if (basic == "Quality" || basic == "QUALITY") {
        if (value.kind() != K::bit_string) return false;
        const auto& bits = value.raw_value();
        // IEC 61850 q has precisely 13 defined bits; a short/overlong or
        // nonzero padding value cannot authorize a semantic quality label.
        return bits.size() == 3U && bits[0] == 3U &&
               (bits[2] & 0x07U) == 0U;
    }
    if (basic == "Timestamp" || basic == "TIMESTAMP") return value.kind() == K::utc_time;
    if (basic == "Enum" || basic == "ENUM" || basic == "INT32" ||
        basic == "INT8" || basic == "INT16") return value.kind() == K::integer;
    if (basic == "FLOAT32" || basic == "FLOAT64")
        return value.kind() == K::floating_point;
    // Unknown/vendor type cannot authorize semantic labelling.
    return false;
}
inline std::optional<std::vector<VerifiedReportLeaf>> bindFlatSclReportStructure(
    const semantic_mms::MmsLiveModelDocument& model,
    const std::string_view dataSet,
    const semantic_mms::MmsReportValue& member) {
    using namespace semantic_mms;
    if (!member.value || member.value->kind() != MmsDataKind::structure)
        return std::nullopt;
    // SCL identity/model must be trusted and the FCDA directory must be
    // uniquely bound to this DataSet and exact ordinal. Never infer by RptID.
    if (model.identity.confidence != MmsLiveModelConfidence::exact ||
        model.source != "TrustedSclInitialSnapshot" || dataSet.empty())
        return std::nullopt;
    const MmsLiveDataSet* chosen = nullptr;
    for (const auto& ds : model.data_sets) {
        if (ds.reference != dataSet) continue;
        if (chosen) return std::nullopt;
        chosen = &ds;
    }
    if (!chosen) return std::nullopt;
    const MmsLiveDataSetMember* binding = nullptr;
    for (const auto& fcda : chosen->members) {
        if (fcda.index != member.data_set_index) continue;
        if (binding) return std::nullopt;
        binding = &fcda;
    }
    if (!binding || binding->confidence != MmsLiveModelConfidence::exact ||
        binding->reference.empty() || binding->functional_constraint.empty())
        return std::nullopt;
    const auto& raw = member.data_reference;
    const auto& dir = member.member;
    // Respect the aliases accepted by the native frame validator. DataRef
    // may use IEC notation (LD/LN.DO) or MMS notation (LD/LN$FC$DO).
    // It must still resolve to the exact SCL ordinal/reference/FC.
    const auto matchesExactMember = [&](const std::string_view value) {
        if (value.empty()) return false;
        if (value == binding->reference) return true;
        // Even the SCL's MMS alias must reduce to this exact FCDA identity.
        // A stale/incorrect alias must never certify a foreign member.
        const auto slash = value.find('/');
        if (slash == std::string_view::npos || slash == 0U) return false;
        const auto fcStart = value.find('
    if (dir && !dir->functional_constraint.empty() &&
        dir->functional_constraint != binding->functional_constraint)
        return std::nullopt;
    // Flat ST/CF/SG structures only. Nested, array, repeated or vendor
    // layouts have no proven ordered schema here and remain positional.
    const MmsLiveDataObject* object = nullptr;
    for (const auto& ld : model.logical_devices)
        for (const auto& ln : ld.logical_nodes)
            for (const auto& candidate : ln.data_objects)
                if (binding->reference == candidate.reference) {
                    if (object) return std::nullopt;
                    object = &candidate;
                }
    if (!object || object->confidence != MmsLiveModelConfidence::exact)
        return std::nullopt;
    std::vector<const MmsLiveDataAttribute*> attributes;
    for (const auto& attribute : object->attributes) {
        if (attribute.functional_constraint != binding->functional_constraint ||
            !semanticInside(attribute.object_reference, binding->reference))
            continue;
        if (attribute.source != "TrustedSCL" ||
            attribute.type_confidence != MmsLiveModelConfidence::exact ||
            attribute.functional_constraint_confidence != MmsLiveModelConfidence::exact)
            return std::nullopt;
        // Only one direct segment is admissible; nested structures demand
        // matching a hierarchical SCL schema, not guesswork.
        const auto suffix = std::string_view{attribute.object_reference}
            .substr(binding->reference.size() + 1);
        if (suffix.empty() || suffix.find('.') != std::string_view::npos)
            return std::nullopt;
        attributes.push_back(&attribute);
    }
    const auto& children = member.value->children();
    if (attributes.empty() || attributes.size() != children.size() ||
        children.size() > 16U) return std::nullopt;
    // Positional order is not independently proven by matching count alone.
    // Reject repeated scalar types, where a vendor permutation would look
    // valid and produce deceptively named engineering values.
    for (std::size_t i = 0; i < attributes.size(); ++i)
        for (std::size_t j = i + 1; j < attributes.size(); ++j)
            if (semanticExpectedKind(attributes[i]->scl_basic_type) ==
                semanticExpectedKind(attributes[j]->scl_basic_type))
                return std::nullopt;
    std::vector<VerifiedReportLeaf> result;
    result.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (!semanticKindCompatible(children[i], attributes[i]->scl_basic_type))
            return std::nullopt;
        if (std::any_of(result.begin(), result.end(), [&](const auto& leaf){
                return leaf.reference == attributes[i]->object_reference; }))
            return std::nullopt;
        result.push_back({attributes[i]->object_reference, &children[i]});
    }
    return result;
}
} // namespace arstack::iedsim
, slash + 1U);
        if (fcStart == std::string_view::npos) return false;
        const auto pathStart = value.find('
    if (dir && !dir->functional_constraint.empty() &&
        dir->functional_constraint != binding->functional_constraint)
        return std::nullopt;
    // Flat ST/CF/SG structures only. Nested, array, repeated or vendor
    // layouts have no proven ordered schema here and remain positional.
    const MmsLiveDataObject* object = nullptr;
    for (const auto& ld : model.logical_devices)
        for (const auto& ln : ld.logical_nodes)
            for (const auto& candidate : ln.data_objects)
                if (binding->reference == candidate.reference) {
                    if (object) return std::nullopt;
                    object = &candidate;
                }
    if (!object || object->confidence != MmsLiveModelConfidence::exact)
        return std::nullopt;
    std::vector<const MmsLiveDataAttribute*> attributes;
    for (const auto& attribute : object->attributes) {
        if (attribute.functional_constraint != binding->functional_constraint ||
            !semanticInside(attribute.object_reference, binding->reference))
            continue;
        if (attribute.source != "TrustedSCL" ||
            attribute.type_confidence != MmsLiveModelConfidence::exact ||
            attribute.functional_constraint_confidence != MmsLiveModelConfidence::exact)
            return std::nullopt;
        // Only one direct segment is admissible; nested structures demand
        // matching a hierarchical SCL schema, not guesswork.
        const auto suffix = std::string_view{attribute.object_reference}
            .substr(binding->reference.size() + 1);
        if (suffix.empty() || suffix.find('.') != std::string_view::npos)
            return std::nullopt;
        attributes.push_back(&attribute);
    }
    const auto& children = member.value->children();
    if (attributes.empty() || attributes.size() != children.size() ||
        children.size() > 16U) return std::nullopt;
    // Positional order is not independently proven by matching count alone.
    // Reject repeated scalar types, where a vendor permutation would look
    // valid and produce deceptively named engineering values.
    for (std::size_t i = 0; i < attributes.size(); ++i)
        for (std::size_t j = i + 1; j < attributes.size(); ++j)
            if (semanticExpectedKind(attributes[i]->scl_basic_type) ==
                semanticExpectedKind(attributes[j]->scl_basic_type))
                return std::nullopt;
    std::vector<VerifiedReportLeaf> result;
    result.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (!semanticKindCompatible(children[i], attributes[i]->scl_basic_type))
            return std::nullopt;
        if (std::any_of(result.begin(), result.end(), [&](const auto& leaf){
                return leaf.reference == attributes[i]->object_reference; }))
            return std::nullopt;
        result.push_back({attributes[i]->object_reference, &children[i]});
    }
    return result;
}
} // namespace arstack::iedsim
, fcStart + 1U);
        if (pathStart == std::string_view::npos ||
            value.substr(fcStart + 1U, pathStart - fcStart - 1U) !=
                binding->functional_constraint)
            return false;
        std::string canonical{value.substr(0, fcStart)};
        canonical.push_back('.');
        for (std::size_t i = pathStart + 1U; i < value.size(); ++i)
            canonical.push_back(value[i] == '
    if (dir && !dir->functional_constraint.empty() &&
        dir->functional_constraint != binding->functional_constraint)
        return std::nullopt;
    // Flat ST/CF/SG structures only. Nested, array, repeated or vendor
    // layouts have no proven ordered schema here and remain positional.
    const MmsLiveDataObject* object = nullptr;
    for (const auto& ld : model.logical_devices)
        for (const auto& ln : ld.logical_nodes)
            for (const auto& candidate : ln.data_objects)
                if (binding->reference == candidate.reference) {
                    if (object) return std::nullopt;
                    object = &candidate;
                }
    if (!object || object->confidence != MmsLiveModelConfidence::exact)
        return std::nullopt;
    std::vector<const MmsLiveDataAttribute*> attributes;
    for (const auto& attribute : object->attributes) {
        if (attribute.functional_constraint != binding->functional_constraint ||
            !semanticInside(attribute.object_reference, binding->reference))
            continue;
        if (attribute.source != "TrustedSCL" ||
            attribute.type_confidence != MmsLiveModelConfidence::exact ||
            attribute.functional_constraint_confidence != MmsLiveModelConfidence::exact)
            return std::nullopt;
        // Only one direct segment is admissible; nested structures demand
        // matching a hierarchical SCL schema, not guesswork.
        const auto suffix = std::string_view{attribute.object_reference}
            .substr(binding->reference.size() + 1);
        if (suffix.empty() || suffix.find('.') != std::string_view::npos)
            return std::nullopt;
        attributes.push_back(&attribute);
    }
    const auto& children = member.value->children();
    if (attributes.empty() || attributes.size() != children.size() ||
        children.size() > 16U) return std::nullopt;
    // Positional order is not independently proven by matching count alone.
    // Reject repeated scalar types, where a vendor permutation would look
    // valid and produce deceptively named engineering values.
    for (std::size_t i = 0; i < attributes.size(); ++i)
        for (std::size_t j = i + 1; j < attributes.size(); ++j)
            if (semanticExpectedKind(attributes[i]->scl_basic_type) ==
                semanticExpectedKind(attributes[j]->scl_basic_type))
                return std::nullopt;
    std::vector<VerifiedReportLeaf> result;
    result.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (!semanticKindCompatible(children[i], attributes[i]->scl_basic_type))
            return std::nullopt;
        if (std::any_of(result.begin(), result.end(), [&](const auto& leaf){
                return leaf.reference == attributes[i]->object_reference; }))
            return std::nullopt;
        result.push_back({attributes[i]->object_reference, &children[i]});
    }
    return result;
}
} // namespace arstack::iedsim
 ? '.' : value[i]);
        return canonical == binding->reference;
    };
    if (!raw.empty() && !matchesExactMember(raw)) return std::nullopt;
    if (dir && !dir->user_reference.empty() &&
        !matchesExactMember(dir->user_reference)) return std::nullopt;
    if (dir && !dir->mms_reference.empty() &&
        !matchesExactMember(dir->mms_reference)) return std::nullopt;
    if (raw.empty() && (!dir || (dir->user_reference.empty() &&
                                dir->mms_reference.empty())))
        return std::nullopt;
    if (dir && !dir->functional_constraint.empty() &&
        dir->functional_constraint != binding->functional_constraint)
        return std::nullopt;
    // Flat ST/CF/SG structures only. Nested, array, repeated or vendor
    // layouts have no proven ordered schema here and remain positional.
    const MmsLiveDataObject* object = nullptr;
    for (const auto& ld : model.logical_devices)
        for (const auto& ln : ld.logical_nodes)
            for (const auto& candidate : ln.data_objects)
                if (binding->reference == candidate.reference) {
                    if (object) return std::nullopt;
                    object = &candidate;
                }
    if (!object || object->confidence != MmsLiveModelConfidence::exact)
        return std::nullopt;
    std::vector<const MmsLiveDataAttribute*> attributes;
    for (const auto& attribute : object->attributes) {
        if (attribute.functional_constraint != binding->functional_constraint ||
            !semanticInside(attribute.object_reference, binding->reference))
            continue;
        if (attribute.source != "TrustedSCL" ||
            attribute.type_confidence != MmsLiveModelConfidence::exact ||
            attribute.functional_constraint_confidence != MmsLiveModelConfidence::exact)
            return std::nullopt;
        // Only one direct segment is admissible; nested structures demand
        // matching a hierarchical SCL schema, not guesswork.
        const auto suffix = std::string_view{attribute.object_reference}
            .substr(binding->reference.size() + 1);
        if (suffix.empty() || suffix.find('.') != std::string_view::npos)
            return std::nullopt;
        attributes.push_back(&attribute);
    }
    const auto& children = member.value->children();
    if (attributes.empty() || attributes.size() != children.size() ||
        children.size() > 16U) return std::nullopt;
    // Positional order is not independently proven by matching count alone.
    // Reject repeated scalar types, where a vendor permutation would look
    // valid and produce deceptively named engineering values.
    for (std::size_t i = 0; i < attributes.size(); ++i)
        for (std::size_t j = i + 1; j < attributes.size(); ++j)
            if (semanticExpectedKind(attributes[i]->scl_basic_type) ==
                semanticExpectedKind(attributes[j]->scl_basic_type))
                return std::nullopt;
    std::vector<VerifiedReportLeaf> result;
    result.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (!semanticKindCompatible(children[i], attributes[i]->scl_basic_type))
            return std::nullopt;
        if (std::any_of(result.begin(), result.end(), [&](const auto& leaf){
                return leaf.reference == attributes[i]->object_reference; }))
            return std::nullopt;
        result.push_back({attributes[i]->object_reference, &children[i]});
    }
    return result;
}
} // namespace arstack::iedsim
