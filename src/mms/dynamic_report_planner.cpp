// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/dynamic_report_planner.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar::iec61850::mms {
namespace {

constexpr std::size_t maximum_type_tree_depth = 64U;

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

[[nodiscard]] bool same_text(
    const std::string_view left,
    const std::string_view right) {
    return lower_ascii(std::string{left}) == lower_ascii(std::string{right});
}

[[nodiscard]] bool scalar_type(const MmsTypeSpecification& type) noexcept {
    return type.kind != MmsTypeKind::array &&
           type.kind != MmsTypeKind::structure &&
           type.kind != MmsTypeKind::unknown;
}

struct ItemIdentity final {
    std::string logical_node;
    std::string functional_constraint;
    std::string leaf_name;
};

[[nodiscard]] std::optional<ItemIdentity> parse_item_identity(
    const std::string& item) {
    const auto first = item.find('$');
    if (first == std::string::npos || first == 0U) return std::nullopt;
    const auto second = item.find('$', first + 1U);
    if (second == std::string::npos || second == first + 1U) {
        return std::nullopt;
    }
    const auto last = item.rfind('$');
    return ItemIdentity{
        item.substr(0U, first),
        item.substr(first + 1U, second - first - 1U),
        item.substr(last + 1U)};
}

void collect_scalar_leaves(
    const MmsObjectName& root,
    const MmsTypeSpecification& type,
    const std::string& suffix,
    const std::size_t depth,
    std::vector<MmsObjectName>& leaves) {
    if (depth > maximum_type_tree_depth) {
        throw std::runtime_error(
            "MMS variable type tree exceeds the dynamic-report traversal limit.");
    }
    if (scalar_type(type)) {
        auto item = root.item;
        if (!suffix.empty()) item += '$' + suffix;
        leaves.push_back(MmsObjectName::domain_specific(root.domain, std::move(item)));
        return;
    }
    if (type.kind != MmsTypeKind::structure) return;

    for (const auto& child : type.children) {
        if (child.name.empty()) continue;
        const auto child_suffix = suffix.empty()
            ? child.name
            : suffix + '$' + child.name;
        collect_scalar_leaves(root, child, child_suffix, depth + 1U, leaves);
    }
}

[[nodiscard]] int semantic_leaf_score(const std::string& leaf_name) {
    const auto leaf = lower_ascii(leaf_name);
    if (leaf == "stval") return 30;
    if (leaf == "general") return 25;
    if (leaf == "f" || leaf == "i") return 22;
    if (leaf == "mag") return 15;
    if (leaf == "q") return -10;
    if (leaf == "t") return -20;
    return 0;
}

} // namespace

MmsDynamicReportMemberSelection MmsDynamicReportMemberSelector::select(
    const MmsLiveDiscoveryResult& discovery,
    const MmsReportControlCandidate& report_control,
    const std::size_t requested_members) {
    if (requested_members == 0U) {
        throw std::invalid_argument(
            "Dynamic report member count must be positive.");
    }

    struct Ranked final {
        int score{};
        MmsObjectName name;
    };

    MmsDynamicReportMemberSelection result;
    std::vector<Ranked> ranked;
    std::set<std::string, std::less<>> unique;

    for (const auto& evidence : discovery.variable_types) {
        if (!evidence.success()) continue;
        ++result.successful_type_probes;
        const auto& variable = evidence.variable;
        if (variable.kind != MmsObjectNameKind::domain_specific ||
            !same_text(variable.domain, report_control.domain)) {
            continue;
        }

        std::vector<MmsObjectName> leaves;
        collect_scalar_leaves(
            variable, evidence.attributes->type, {}, 0U, leaves);
        for (auto& leaf : leaves) {
            const auto identity = parse_item_identity(leaf.item);
            if (!identity ||
                (!same_text(identity->functional_constraint, "ST") &&
                 !same_text(identity->functional_constraint, "MX"))) {
                continue;
            }
            if (!unique.insert(lower_ascii(leaf.reference())).second) continue;

            int score = same_text(identity->functional_constraint, "ST") ? 40 : 25;
            if (same_text(identity->logical_node, report_control.logical_node)) {
                score += 80;
            }
            if (same_text(identity->logical_node, "LLN0")) score += 10;
            score += semantic_leaf_score(identity->leaf_name);
            score -= static_cast<int>(
                std::min<std::size_t>(leaf.item.size(), 60U) / 10U);
            ranked.push_back({score, std::move(leaf)});
        }
    }

    result.scalar_leaf_candidates = ranked.size();
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.name.reference() < right.name.reference();
    });

    const auto count = std::min(requested_members, ranked.size());
    result.members.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.members.push_back(std::move(ranked[index].name));
    }
    return result;
}

} // namespace ar::iec61850::mms
