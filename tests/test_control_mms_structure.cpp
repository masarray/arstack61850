// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/mms_control_structure.hpp"
#include "ariec61850/mms/data_codec.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace ar::iec61850;
using namespace ar::iec61850::control;
using namespace ar::iec61850::mms;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

std::string to_hex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        result.push_back(digits[(byte >> 4U) & 0x0FU]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

MmsTypeSpecification scalar(
    const MmsTypeKind kind,
    std::string name,
    const std::optional<std::uint32_t> size = std::nullopt) {
    MmsTypeSpecification result;
    result.kind = kind;
    result.name = std::move(name);
    result.size = size;
    return result;
}

MmsTypeSpecification structure(
    std::string name,
    std::vector<MmsTypeSpecification> children) {
    MmsTypeSpecification result;
    result.kind = MmsTypeKind::structure;
    result.name = std::move(name);
    result.children = std::move(children);
    return result;
}

MmsTypeSpecification origin_spec() {
    return structure("origin", {
        scalar(MmsTypeKind::unsigned_integer, "orCat"),
        scalar(MmsTypeKind::octet_string, "orIdent", 64U),
    });
}

MmsTypeSpecification oper_spec() {
    // Deliberately put operTm before origin to prove that live component order,
    // not a locally invented fixed order, drives the resulting MMS structure.
    return structure("Oper", {
        scalar(MmsTypeKind::bit_string, "ctlVal", 2U),
        scalar(MmsTypeKind::utc_time, "operTm"),
        origin_spec(),
        scalar(MmsTypeKind::unsigned_integer, "ctlNum"),
        scalar(MmsTypeKind::utc_time, "T"),
        scalar(MmsTypeKind::boolean, "Test"),
        scalar(MmsTypeKind::bit_string, "Check", 2U),
    });
}

MmsTypeSpecification cancel_spec(const bool include_check) {
    std::vector<MmsTypeSpecification> children{
        scalar(MmsTypeKind::bit_string, "ctlVal", 2U),
        origin_spec(),
        scalar(MmsTypeKind::unsigned_integer, "ctlNum"),
        scalar(MmsTypeKind::utc_time, "T"),
        scalar(MmsTypeKind::boolean, "Test"),
    };
    if (include_check) {
        children.push_back(scalar(MmsTypeKind::bit_string, "Check", 2U));
    }
    return structure("Cancel", std::move(children));
}

MmsControlSequenceContext context() {
    MmsControlSequenceContext result;
    result.control_value = ControlValue::double_point(DoublePointValue::on);
    result.origin_category = OriginCategory::station_control;
    result.origin_identifier = {'H', 'M', 'I'};
    result.control_number = 7U;
    result.timestamp = MmsDataValue::utc_time(Iec61850UtcTime{});
    result.test = false;
    result.synchro_check = true;
    result.interlock_check = true;
    return result;
}

void dpc_binding_matches_network_bit_order() {
    const auto specification = scalar(MmsTypeKind::bit_string, "ctlVal", 2U);
    auto result = MmsControlStructureBuilder::bind_control_value(
        ControlValue::double_point(DoublePointValue::on), specification);
    CHECK(result.success());
    CHECK(result.value->kind() == MmsDataKind::bit_string);
    CHECK(result.value->raw_value().size() == 2U);
    CHECK(result.value->raw_value()[0] == 6U);
    CHECK(result.value->raw_value()[1] == 0x80U); // DPC On == binary 10.

    result = MmsControlStructureBuilder::bind_control_value(
        ControlValue::double_point(DoublePointValue::off), specification);
    CHECK(result.success());
    CHECK(result.value->raw_value()[1] == 0x40U); // DPC Off == binary 01.
}

void operate_matches_csharp_structure_contract_and_golden_wire() {
    const auto specification = oper_spec();
    auto build = MmsControlStructureBuilder::build_operate(context(), specification);
    CHECK(build.success());
    CHECK(build.value->kind() == MmsDataKind::structure);
    CHECK(build.value->children().size() == 7U);

    const auto& children = build.value->children();
    CHECK(children[0].kind() == MmsDataKind::bit_string);
    CHECK(children[0].raw_value() == std::vector<std::uint8_t>({0x06U, 0x80U}));
    CHECK(children[1].kind() == MmsDataKind::utc_time); // default operTm epoch.
    CHECK(children[2].kind() == MmsDataKind::structure);
    CHECK(std::get<std::uint64_t>(children[2].children()[0].value()) == 2U);
    CHECK(children[2].children()[1].raw_value() ==
          std::vector<std::uint8_t>({'H', 'M', 'I'}));
    CHECK(std::get<std::uint64_t>(children[3].value()) == 7U);
    CHECK(children[4].kind() == MmsDataKind::utc_time);
    CHECK(!std::get<bool>(children[5].value()));
    CHECK(children[6].raw_value() == std::vector<std::uint8_t>({0x06U, 0xC0U}));

    const auto encoded = MmsDataCodec::encode(build.value.value());
    CHECK(to_hex(encoded) ==
          "A22C8402068091080000000000000000A2088601028903484D4986010791080000000000000000830100840206C0");

    const auto decoded = MmsDataCodec::decode_all(encoded);
    CHECK(decoded.size() == 1U);
    CHECK(decoded.front().kind() == MmsDataKind::structure);
    CHECK(decoded.front().children().size() == 7U);
    CHECK(decoded.front().children()[6].raw_value() ==
          std::vector<std::uint8_t>({0x06U, 0xC0U}));
}

void sbow_uses_same_exact_sequence_contract() {
    auto specification = oper_spec();
    specification.name = "SBOw";
    auto input = context();
    input.operate_at = MmsDataValue::utc_time(Iec61850UtcTime{
        std::chrono::system_clock::time_point{std::chrono::seconds{10}}, 0U});
    const auto build = MmsControlStructureBuilder::build_select_with_value(
        input, specification);
    CHECK(build.success());
    CHECK(build.value->children().size() == 7U);
    CHECK(build.value->children()[1].kind() == MmsDataKind::utc_time);
}

void cancel_allows_optional_check_but_not_vendor_guessing() {
    auto build = MmsControlStructureBuilder::build_cancel(context(), cancel_spec(false));
    CHECK(build.success());
    CHECK(build.value->children().size() == 5U);

    build = MmsControlStructureBuilder::build_cancel(context(), cancel_spec(true));
    CHECK(build.success());
    CHECK(build.value->children().size() == 6U);
    CHECK(build.value->children().back().raw_value() ==
          std::vector<std::uint8_t>({0x06U, 0xC0U}));

    auto vendor = cancel_spec(false);
    vendor.children.push_back(scalar(MmsTypeKind::boolean, "vendorMagic"));
    build = MmsControlStructureBuilder::build_cancel(context(), vendor);
    CHECK(build.status == MmsControlBuildStatus::unsupported_component);
    CHECK(build.path == "vendorMagic");
}

void exact_required_fields_and_check_shape_fail_closed() {
    auto missing_check = oper_spec();
    missing_check.children.pop_back();
    auto build = MmsControlStructureBuilder::build_operate(context(), missing_check);
    CHECK(build.status == MmsControlBuildStatus::missing_required_component);
    CHECK(build.path == "check");

    auto wrong_check = oper_spec();
    wrong_check.children.back().size = 3U;
    build = MmsControlStructureBuilder::build_operate(context(), wrong_check);
    CHECK(build.status == MmsControlBuildStatus::shape_mismatch);
    CHECK(build.path == "Check");

    auto missing_timestamp = context();
    missing_timestamp.timestamp.reset();
    build = MmsControlStructureBuilder::build_operate(missing_timestamp, oper_spec());
    CHECK(build.status == MmsControlBuildStatus::timestamp_missing);
}

void origin_ctl_num_and_raw_values_are_live_type_checked() {
    auto bad_origin = oper_spec();
    bad_origin.children[2].children[0].kind = MmsTypeKind::boolean;
    auto build = MmsControlStructureBuilder::build_operate(context(), bad_origin);
    CHECK(build.status == MmsControlBuildStatus::type_mismatch);
    CHECK(build.path == "origin.orCat");

    auto bad_ctl_num = oper_spec();
    bad_ctl_num.children[3].kind = MmsTypeKind::visible_string;
    build = MmsControlStructureBuilder::build_operate(context(), bad_ctl_num);
    CHECK(build.status == MmsControlBuildStatus::type_mismatch);
    CHECK(build.path == "ctlNum");

    const auto raw_good = ControlValue::raw_mms(MmsDataValue::boolean(true));
    auto raw_result = MmsControlStructureBuilder::bind_control_value(
        raw_good, scalar(MmsTypeKind::boolean, "ctlVal"));
    CHECK(raw_result.success());
    raw_result = MmsControlStructureBuilder::bind_control_value(
        raw_good, scalar(MmsTypeKind::integer, "ctlVal"));
    CHECK(raw_result.status == MmsControlBuildStatus::type_mismatch);
}

void step_position_and_analogue_structures_follow_live_shape() {
    const auto step_specification = structure("ctlVal", {
        scalar(MmsTypeKind::integer, "posVal"),
        scalar(MmsTypeKind::boolean, "transInd"),
    });
    auto result = MmsControlStructureBuilder::bind_control_value(
        ControlValue::step_position({-3, true}), step_specification);
    CHECK(result.success());
    CHECK(std::get<std::int64_t>(result.value->children()[0].value()) == -3);
    CHECK(std::get<bool>(result.value->children()[1].value()));

    const auto analogue_specification = structure("ctlVal", {
        scalar(MmsTypeKind::floating_point, "f"),
        scalar(MmsTypeKind::boolean, "qualityHint"),
    });
    result = MmsControlStructureBuilder::bind_control_value(
        ControlValue::floating_point(12.5), analogue_specification);
    CHECK(result.success());
    CHECK(std::get<double>(result.value->children()[0].value()) == 12.5);
    CHECK(!std::get<bool>(result.value->children()[1].value()));
}

} // namespace

int main() {
    try {
        dpc_binding_matches_network_bit_order();
        operate_matches_csharp_structure_contract_and_golden_wire();
        sbow_uses_same_exact_sequence_contract();
        cancel_allows_optional_check_but_not_vendor_guessing();
        exact_required_fields_and_check_shape_fail_closed();
        origin_ctl_num_and_raw_values_are_live_type_checked();
        step_position_and_analogue_structures_follow_live_shape();
        std::cout << "Control MMS structure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
