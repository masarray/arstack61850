// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <array>
#include <cctype>

namespace ar::iec61850::mms {

struct MmsLogicalNodeName final {
    std::string name;
    std::string prefix;
    std::string logical_node_class;
    std::string instance;

    friend bool operator==(const MmsLogicalNodeName&, const MmsLogicalNodeName&) = default;
};

struct MmsLivePointReference final {
    std::string domain;
    std::string logical_node;
    std::string functional_constraint;
    std::string data_object_path;
    std::string mms_item_name;
    std::string source{"LiveMmsGetNameList"};
    std::uint32_t confidence{100U};

    [[nodiscard]] std::string user_path() const;
    [[nodiscard]] std::string user_reference() const;
    [[nodiscard]] std::string mms_reference() const;
    [[nodiscard]] bool report_attribute() const noexcept;
    [[nodiscard]] bool control_attribute() const noexcept;

    friend bool operator==(const MmsLivePointReference&,
                           const MmsLivePointReference&) = default;
};

class MmsLiveReferenceParser final {
public:
    [[nodiscard]] static bool known_functional_constraint(
        std::string_view value) noexcept;
    [[nodiscard]] static std::string normalize_functional_constraint(
        std::string_view value);

    [[nodiscard]] static std::optional<MmsLivePointReference> parse_variable(
        std::string_view domain,
        std::string_view raw_mms_name,
        std::string source = "LiveMmsGetNameList",
        std::uint32_t confidence = 100U);

    [[nodiscard]] static MmsLogicalNodeName parse_logical_node_name(
        std::string_view logical_node_name);
    [[nodiscard]] static std::string top_data_object_name(
        std::string_view data_object_path);
    [[nodiscard]] static std::string data_attribute_path(
        std::string_view data_object_path);
};

} // namespace ar::iec61850::mms

#include "ariec61850/mms/model_reference.ipp"
