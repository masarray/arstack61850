// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/dynamic_report_planner.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_codec.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

[[nodiscard]] mms::MmsTypeSpecification scalar(
    const mms::MmsTypeKind kind,
    std::string name) {
    mms::MmsTypeSpecification result;
    result.kind = kind;
    result.name = std::move(name);
    return result;
}

[[nodiscard]] mms::MmsTypeSpecification structure(
    std::string name,
    std::vector<mms::MmsTypeSpecification> children) {
    mms::MmsTypeSpecification result;
    result.kind = mms::MmsTypeKind::structure;
    result.name = std::move(name);
    result.children = std::move(children);
    return result;
}

[[nodiscard]] mms::MmsVariableTypeEvidence evidence(
    std::string domain,
    std::string item,
    mms::MmsTypeSpecification type) {
    mms::MmsVariableTypeEvidence result;
    result.variable = mms::MmsObjectName::domain_specific(
        std::move(domain), std::move(item));
    mms::MmsVariableAccessAttributesResponse attributes;
    attributes.type = std::move(type);
    result.attributes = std::move(attributes);
    return result;
}

[[nodiscard]] mms::MmsReportControlCandidate report_control() {
    mms::MmsReportControlCandidate result;
    result.domain = "IEDLD0";
    result.logical_node = "LLN0";
    result.reference = "IEDLD0/LLN0.BR.brcbA01";
    result.buffered = true;
    return result;
}

void logical_node_tree_projects_scalar_st_mx_leaves() {
    mms::MmsLiveDiscoveryResult discovery;
    discovery.variable_types.push_back(evidence(
        "IEDLD0", "LLN0", structure("", {
            structure("ST", {
                structure("Beh", {
                    scalar(mms::MmsTypeKind::boolean, "stVal"),
                    scalar(mms::MmsTypeKind::bit_string, "q")})}),
            structure("MX", {
                structure("Hz", {
                    structure("mag", {
                        scalar(mms::MmsTypeKind::floating_point, "f")})})}),
            structure("CF", {
                scalar(mms::MmsTypeKind::unsigned_integer, "configRev")})})
    ));

    mms::MmsTypeSpecification array;
    array.kind = mms::MmsTypeKind::array;
    array.name = "samples";
    array.children = {scalar(mms::MmsTypeKind::integer, "element")};
    discovery.variable_types.push_back(evidence(
        "IEDLD0", "GGIO1", structure("", {
            structure("ST", {
                structure("Ind1", {
                    scalar(mms::MmsTypeKind::boolean, "stVal"), array})})})
    ));
    discovery.variable_types.push_back(evidence(
        "OTHERLD", "LLN0$ST$Mod$stVal",
        scalar(mms::MmsTypeKind::boolean, "")));

    const auto selected = mms::MmsDynamicReportMemberSelector::select(
        discovery, report_control(), 8U);
    CHECK(selected.successful_type_probes == 3U);
    CHECK(selected.scalar_leaf_candidates == 4U);
    CHECK(selected.members.size() == 4U);
    CHECK(selected.members[0].reference() == "IEDLD0/LLN0$ST$Beh$stVal");
    CHECK(selected.members[1].reference() == "IEDLD0/LLN0$MX$Hz$mag$f");
    CHECK(selected.members[2].reference() == "IEDLD0/LLN0$ST$Beh$q");
    CHECK(selected.members[3].reference() == "IEDLD0/GGIO1$ST$Ind1$stVal");
}

void direct_scalar_evidence_and_requested_bound_are_preserved() {
    mms::MmsLiveDiscoveryResult discovery;
    discovery.variable_types.push_back(evidence(
        "iedld0", "LLN0$st$Mod$stVal",
        scalar(mms::MmsTypeKind::boolean, "")));
    discovery.variable_types.push_back(evidence(
        "IEDLD0", "LLN0$ST$Beh$stVal",
        scalar(mms::MmsTypeKind::boolean, "")));

    const auto selected = mms::MmsDynamicReportMemberSelector::select(
        discovery, report_control(), 1U);
    CHECK(selected.scalar_leaf_candidates == 2U);
    CHECK(selected.members.size() == 1U);
    CHECK(selected.members.front().reference() ==
          "IEDLD0/LLN0$ST$Beh$stVal");
}

void report_dataset_attribute_uses_mms_wire_reference() {
    CHECK(mms::MmsDataSetDirectoryCodec::to_report_attribute_value(
              "IEDLD0/LLN0.DynamicSet") ==
          "IEDLD0/LLN0$DynamicSet");
    CHECK(mms::MmsDataSetDirectoryCodec::to_report_attribute_value(
              "IEDLD0/LLN0$DynamicSet") ==
          "IEDLD0/LLN0$DynamicSet");
    CHECK(mms::MmsDataSetDirectoryCodec::to_report_attribute_value({}).empty());
}

void information_report_variable_list_name_is_supported() {
    using ar::iec61850::asn1::BerWriter;
    const auto object_name = mms::MmsServiceCodec::encode_object_name(
        mms::MmsObjectName::domain_specific("IEDLD0", "LLN0$DynamicSet"));
    const auto report_value = mms::MmsDataCodec::encode(
        mms::MmsDataValue::visible_string("IEDLD0/LLN0.BR.brcbA01"));
    const auto result_list = BerWriter::encode_tlv(0xA0U, report_value);
    const std::array specifications{
        BerWriter::encode_tlv(0xA1U, object_name),
        object_name};
    for (const auto& specification : specifications) {
        auto body = specification;
        body.insert(body.end(), result_list.begin(), result_list.end());
        const auto information_report = BerWriter::encode_tlv(0xA0U, body);
        const auto pdu = BerWriter::encode_tlv(0xA3U, information_report);
        const auto decoded = mms::MmsInformationReportCodec::decode(pdu);
        CHECK(decoded.variable_references.size() == 1U);
        CHECK(decoded.variable_references.front().reference() ==
              "IEDLD0/LLN0$DynamicSet");
        CHECK(decoded.items.size() == 1U);
    }
}

} // namespace

int main() {
    try {
        logical_node_tree_projects_scalar_st_mx_leaves();
        direct_scalar_evidence_and_requested_bound_are_preserved();
        report_dataset_attribute_uses_mms_wire_reference();
        information_report_variable_list_name_is_supported();
        std::cout << "Dynamic report member planner tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
