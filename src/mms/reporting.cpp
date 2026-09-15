// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/reporting.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/pdu.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>

namespace ar::iec61850::mms {
namespace {

using asn1::BerClass;
using asn1::BerReader;
using asn1::BerTlv;
using asn1::BerWriter;

constexpr std::int32_t k_get_named_variable_list_attributes = 12;

std::vector<std::string> split(const std::string& text, const char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0U;
    while (start <= text.size()) {
        const auto end = text.find(delimiter, start);
        parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return parts;
}

std::string join(const std::span<const std::string> parts, const char delimiter) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0U) result.push_back(delimiter);
        result += parts[i];
    }
    return result;
}

void validate_ascii(const std::string& value, const char* field, const bool allow_empty = false) {
    if ((!allow_empty && value.empty()) || value.size() > MmsServiceCodec::maximum_identifier_bytes) {
        throw MmsReportingFormatError(std::string(field) + " length is outside the configured limit.");
    }
    if (std::any_of(value.begin(), value.end(), [](char ch) {
            const auto byte = static_cast<unsigned char>(ch);
            return byte == 0U || byte > 0x7FU;
        })) {
        throw MmsReportingFormatError(std::string(field) + " must contain ASCII bytes.");
    }
}

std::vector<std::uint8_t> positive_integer_content(const std::uint64_t value) {
    auto bytes = BerWriter::encode_unsigned_integer(value);
    if (bytes.empty()) bytes.push_back(0U);
    if ((bytes.front() & 0x80U) != 0U) bytes.insert(bytes.begin(), 0U);
    return bytes;
}

std::uint32_t read_u32(const BerTlv& tlv, const char* field) {
    const auto value = BerReader::read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw MmsReportingFormatError(std::string(field) + " is not an unsigned 32-bit value.");
    }
    return static_cast<std::uint32_t>(*value);
}

std::vector<std::uint8_t> concat(const std::initializer_list<std::span<const std::uint8_t>> parts) {
    std::size_t total = 0U;
    for (const auto part : parts) {
        if (part.size() > MmsPduCodec::maximum_pdu_bytes - total) {
            throw MmsReportingFormatError("MMS reporting encoding exceeds the configured PDU limit.");
        }
        total += part.size();
    }
    std::vector<std::uint8_t> out;
    out.reserve(total);
    for (const auto part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}

bool is_mms_data(const BerTlv& tlv) noexcept {
    if (tlv.tag_class != BerClass::context_specific) return false;
    if (tlv.tag_number == 1 || tlv.tag_number == 2) return tlv.constructed;
    return tlv.tag_number >= 3 && tlv.tag_number <= 17 && !tlv.constructed;
}

std::optional<std::uint64_t> unsigned_value(const MmsDataValue& value) {
    if (value.kind() == MmsDataKind::unsigned_integer) {
        if (const auto* number = std::get_if<std::uint64_t>(&value.value())) return *number;
    }
    if (value.kind() == MmsDataKind::integer) {
        if (const auto* number = std::get_if<std::int64_t>(&value.value()); number && *number >= 0) {
            return static_cast<std::uint64_t>(*number);
        }
    }
    return std::nullopt;
}

std::optional<bool> boolean_value(const MmsDataValue& value) {
    if (value.kind() != MmsDataKind::boolean) return std::nullopt;
    if (const auto* result = std::get_if<bool>(&value.value())) return *result;
    return std::nullopt;
}

std::optional<std::string> string_value(const MmsDataValue& value) {
    if (value.kind() != MmsDataKind::visible_string && value.kind() != MmsDataKind::mms_string) {
        return std::nullopt;
    }
    if (const auto* result = std::get_if<std::string>(&value.value())) return *result;
    return std::nullopt;
}

std::vector<std::uint8_t> octets(const MmsDataValue& value) {
    if (value.kind() == MmsDataKind::octet_string || value.kind() == MmsDataKind::binary_time ||
        value.kind() == MmsDataKind::bit_string) {
        return value.raw_value();
    }
    return {};
}

MmsReportBitField decode_bit_field(
    const MmsDataValue& value,
    const std::span<const char* const> names) {
    if (value.kind() != MmsDataKind::bit_string || value.raw_value().empty()) {
        throw MmsReportingFormatError("Expected MMS BIT STRING value.");
    }
    MmsReportBitField result;
    result.raw = value.raw_value();
    const auto unused = result.raw.front();
    if (unused > 7U) throw MmsReportingFormatError("MMS BIT STRING unused-bit count is invalid.");
    const auto bit_count = (result.raw.size() - 1U) * 8U - unused;
    for (std::size_t bit = 0U; bit < bit_count; ++bit) {
        const auto byte = result.raw[1U + bit / 8U];
        if ((byte & static_cast<std::uint8_t>(0x80U >> (bit % 8U))) == 0U) continue;
        result.set_bit_indexes.push_back(bit);
        if (bit < names.size() && names[bit] != nullptr) result.names.emplace_back(names[bit]);
    }
    return result;
}

std::vector<std::size_t> bit_indexes(const MmsDataValue& value, const std::size_t maximum_bits) {
    static constexpr std::array<const char*, 1> no_names{nullptr};
    const auto field = decode_bit_field(value, no_names);
    if (value.raw_value().empty()) return {};
    const auto unused = value.raw_value().front();
    const auto bit_count = (value.raw_value().size() - 1U) * 8U - unused;
    if (bit_count > maximum_bits) {
        throw MmsReportingFormatError("MMS report inclusion bit string exceeds the configured limit.");
    }
    return field.set_bit_indexes;
}

const MmsInformationReportItem& require_item(const MmsInformationReport& report, const std::size_t index) {
    if (index >= report.items.size()) throw MmsReportingFormatError("MMS report access-result list is truncated.");
    return report.items[index];
}

const MmsDataValue& require_value(const MmsInformationReport& report, const std::size_t index, const char* field) {
    const auto& item = require_item(report, index);
    if (!item.value) throw MmsReportingFormatError(std::string("MMS report field ") + field + " is a failure result.");
    return *item.value;
}


std::string normalize_data_set_item(const std::string& item) {
    auto parts = split(item, '$');
    if (parts.size() < 2U) return item;
    return parts.front() + "." + join(std::span<const std::string>(parts).subspan(1U), '.');
}

MmsDataSetDirectoryMember normalize_member(const MmsObjectName& name) {
    MmsDataSetDirectoryMember member;
    member.object_name = name;
    member.mms_reference = name.reference();
    member.user_reference = name.reference();
    member.confidence = 25U;
    if (name.kind != MmsObjectNameKind::domain_specific) return member;
    const auto parts = split(name.item, '$');
    if (parts.size() >= 3U) {
        member.logical_node = parts[0];
        member.functional_constraint = parts[1];
        member.data_object_path = join(std::span<const std::string>(parts).subspan(2U), '.');
        member.user_reference = name.domain + "/" + member.logical_node + "." + member.data_object_path;
        member.confidence = 100U;
    } else if (parts.size() == 2U) {
        member.logical_node = parts[0];
        member.data_object_path = parts[1];
        member.user_reference = name.domain + "/" + member.logical_node + "." + member.data_object_path;
        member.confidence = 65U;
    }
    return member;
}

std::vector<std::uint8_t> encode_variable_definition(const MmsObjectName& name) {
    const auto encoded_name = MmsServiceCodec::encode_object_name(name);
    const auto wrapper = BerWriter::encode_tlv(0xA0U, encoded_name);
    return BerWriter::encode_tlv(0x30U, wrapper);
}

MmsObjectName decode_variable_definition(const BerTlv& definition) {
    if (definition.encoded_tag != 0x30U || !definition.constructed) {
        throw MmsReportingFormatError("MMS variable-list member is not a SEQUENCE.");
    }
    const auto fields = BerReader::read_children(definition.value);
    if (fields.size() != 1U || fields[0].encoded_tag != 0xA0U || !fields[0].constructed) {
        throw MmsReportingFormatError("MMS variable-list member has an invalid variableSpecification.");
    }
    return MmsServiceCodec::decode_object_name(fields[0].value);
}

MmsObjectName decode_variable_list_name(const BerTlv& specification) {
    // VariableAccessSpecification.variableListName [1] is found in both
    // explicit-wrapper and implicit ObjectName forms on real IEC 61850 IEDs.
    // First accept a nested ObjectName, then reconstruct the implicit choice.
    try {
        return MmsServiceCodec::decode_object_name(specification.value);
    } catch (const std::exception&) {
        const auto encoded = BerWriter::encode_tlv(
            specification.encoded_tag, specification.value);
        return MmsServiceCodec::decode_object_name(encoded);
    }
}


std::string continuity_message(const MmsReportContinuityEventKind kind) {
    switch (kind) {
    case MmsReportContinuityEventKind::first_report: return "First report observed.";
    case MmsReportContinuityEventKind::in_order: return "Report sequence is in order.";
    case MmsReportContinuityEventKind::duplicate: return "Duplicate report sequence observed.";
    case MmsReportContinuityEventKind::sequence_gap: return "Report sequence gap detected.";
    case MmsReportContinuityEventKind::sequence_wrap: return "Report sequence wrapped.";
    case MmsReportContinuityEventKind::sequence_reset: return "Report sequence reset detected.";
    case MmsReportContinuityEventKind::configuration_revision_changed: return "Configuration revision changed.";
    case MmsReportContinuityEventKind::data_set_changed: return "DataSet reference changed.";
    case MmsReportContinuityEventKind::buffer_overflow: return "Buffered report overflow flag is set.";
    case MmsReportContinuityEventKind::segmentation_started: return "Segmented report started.";
    case MmsReportContinuityEventKind::segmentation_continued: return "Segmented report continued.";
    case MmsReportContinuityEventKind::segmentation_completed: return "Segmented report completed.";
    case MmsReportContinuityEventKind::segmentation_gap: return "Segmented report continuity gap detected.";
    }
    return {};
}

void add_event(std::vector<MmsReportContinuityEvent>& events, const MmsReportContinuityEventKind kind) {
    events.push_back({kind, continuity_message(kind)});
}

} // namespace

std::string MmsReportControlCandidate::mode() const { return buffered ? "BRCB" : "URCB"; }

MmsObjectName MmsReportControlCandidate::attribute_object_name(std::string attribute) const {
    validate_ascii(attribute, "RCB attribute");
    return MmsObjectName::domain_specific(domain, logical_node + "$" + functional_constraint + "$" + name + "$" + attribute);
}

std::size_t MmsReportInventory::buffered_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(report_controls.begin(), report_controls.end(), [](const auto& item) { return item.buffered; }));
}

std::size_t MmsReportInventory::unbuffered_count() const noexcept { return report_controls.size() - buffered_count(); }

MmsReportInventory MmsReportInventoryBuilder::build(const MmsDiscoverySnapshot& snapshot) {
    if (snapshot.domain_variables.size() > maximum_domains || snapshot.domain_variable_lists.size() > maximum_domains) {
        throw MmsReportingFormatError("MMS discovery domain count exceeds the configured limit.");
    }
    MmsReportInventory result;
    for (const auto& [domain, lists] : snapshot.domain_variable_lists) {
        validate_ascii(domain, "MMS discovery domain");
        if (lists.size() > maximum_names_per_domain) throw MmsReportingFormatError("MMS DataSet name count exceeds the configured limit.");
        std::set<std::string, std::less<>> unique;
        for (const auto& raw : lists) {
            validate_ascii(raw, "MMS DataSet name");
            if (!unique.insert(raw).second) continue;
            const auto parts = split(raw, '$');
            MmsDataSetCandidate item;
            item.domain = domain;
            item.raw_mms_name = raw;
            item.logical_node = parts.empty() ? std::string{} : parts.front();
            item.name = parts.size() >= 2U ? join(std::span<const std::string>(parts).subspan(1U), '$') : raw;
            item.reference = domain + "/" + normalize_data_set_item(raw);
            result.data_sets.push_back(std::move(item));
            if (result.data_sets.size() + result.report_controls.size() > maximum_inventory_items) {
                throw MmsReportingFormatError("MMS report inventory exceeds the configured item limit.");
            }
        }
    }

    using Key = std::tuple<std::string, std::string, std::string, std::string>;
    std::map<Key, MmsReportControlCandidate> controls;
    for (const auto& [domain, variables] : snapshot.domain_variables) {
        validate_ascii(domain, "MMS discovery domain");
        if (variables.size() > maximum_names_per_domain) throw MmsReportingFormatError("MMS variable count exceeds the configured limit.");
        for (const auto& raw : variables) {
            validate_ascii(raw, "MMS variable name");
            const auto parts = split(raw, '$');
            if (parts.size() < 4U || (parts[1] != "BR" && parts[1] != "RP")) continue;
            const Key key{domain, parts[0], parts[1], parts[2]};
            auto& control = controls[key];
            control.domain = domain;
            control.logical_node = parts[0];
            control.functional_constraint = parts[1];
            control.name = parts[2];
            control.buffered = parts[1] == "BR";
            control.reference = domain + "/" + parts[0] + "." + parts[2];
            const auto attribute = join(std::span<const std::string>(parts).subspan(3U), '$');
            if (std::find(control.attributes.begin(), control.attributes.end(), attribute) == control.attributes.end()) {
                control.attributes.push_back(attribute);
            }
        }
    }
    for (auto& [_, control] : controls) {
        std::sort(control.attributes.begin(), control.attributes.end());
        if (std::find(control.attributes.begin(), control.attributes.end(), "RptEna") == control.attributes.end()) {
            control.diagnostics.emplace_back("RptEna was not discovered for this report control block.");
        }
        result.report_controls.push_back(std::move(control));
    }
    if (result.data_sets.size() + result.report_controls.size() > maximum_inventory_items) {
        throw MmsReportingFormatError("MMS report inventory exceeds the configured item limit.");
    }
    return result;
}

MmsObjectName MmsDataSetDirectoryCodec::parse_data_set_reference(const std::string& reference) {
    const auto slash = reference.find('/');
    if (slash == std::string::npos) return MmsObjectName::vmd(reference);
    const auto domain = reference.substr(0U, slash);
    auto item = reference.substr(slash + 1U);
    const auto dot = item.find('.');
    if (dot != std::string::npos) item[dot] = '$';
    return MmsObjectName::domain_specific(domain, item);
}

std::string MmsDataSetDirectoryCodec::to_iec_reference(const MmsObjectName& name) {
    if (name.kind != MmsObjectNameKind::domain_specific) return name.reference();
    return name.domain + "/" + normalize_data_set_item(name.item);
}

std::string MmsDataSetDirectoryCodec::to_report_attribute_value(
    const std::string& reference) {
    if (reference.empty()) return {};
    return parse_data_set_reference(reference).reference();
}

std::vector<std::uint8_t> MmsDataSetDirectoryCodec::encode_request_pdu(const MmsDataSetDirectoryRequest& request) {
    const auto object = MmsServiceCodec::encode_object_name(request.data_set_name);
    return MmsPduCodec::encode_confirmed_request({request.invoke_id, k_get_named_variable_list_attributes, true, object});
}

std::vector<std::uint8_t> MmsDataSetDirectoryCodec::encode_request_p_data(
    const MmsDataSetDirectoryRequest& request, const std::uint32_t context) {
    const auto pdu = encode_request_pdu(request);
    return MmsPduCodec::wrap_p_data(pdu, context);
}

MmsDataSetDirectoryRequest MmsDataSetDirectoryCodec::decode_request(const std::span<const std::uint8_t> payload) {
    const auto mms = MmsPduCodec::extract_mms_payload(payload);
    const auto request = MmsPduCodec::decode_confirmed_request(mms);
    if (request.service_tag != k_get_named_variable_list_attributes || !request.service_constructed) {
        throw MmsReportingFormatError("MMS request is not GetNamedVariableListAttributes.");
    }
    return {request.invoke_id, MmsServiceCodec::decode_object_name(request.service_value)};
}

std::vector<std::uint8_t> MmsDataSetDirectoryCodec::encode_response_pdu(const MmsDataSetDirectoryResponse& response) {
    if (response.members.size() > maximum_members) throw MmsReportingFormatError("MMS DataSet directory exceeds the configured member limit.");
    const std::array<std::uint8_t, 1> flag{static_cast<std::uint8_t>(response.deletable ? 0xFFU : 0x00U)};
    const auto deletable = BerWriter::encode_tlv(BerClass::context_specific, false, 0, flag);
    std::vector<std::uint8_t> members;
    for (const auto& member : response.members) {
        const auto encoded = encode_variable_definition(member.object_name);
        members.insert(members.end(), encoded.begin(), encoded.end());
    }
    const auto list = BerWriter::encode_tlv(0xA1U, members);
    const auto body = concat({deletable, list});
    return MmsPduCodec::encode_confirmed_response({response.invoke_id, k_get_named_variable_list_attributes, true, body});
}

std::vector<std::uint8_t> MmsDataSetDirectoryCodec::encode_response_p_data(
    const MmsDataSetDirectoryResponse& response, const std::uint32_t context) {
    const auto pdu = encode_response_pdu(response);
    return MmsPduCodec::wrap_p_data(pdu, context);
}

MmsDataSetDirectoryResponse MmsDataSetDirectoryCodec::decode_response(
    const std::span<const std::uint8_t> payload, const std::optional<std::uint32_t> expected) {
    const auto mms = MmsPduCodec::extract_mms_payload(payload);
    const auto response = MmsPduCodec::decode_confirmed_response(mms);
    if (expected && response.invoke_id != *expected) throw MmsReportingFormatError("MMS DataSet directory invoke ID mismatch.");
    if (response.service_tag != k_get_named_variable_list_attributes || !response.service_constructed) {
        throw MmsReportingFormatError("MMS response is not GetNamedVariableListAttributes.");
    }
    const auto fields = BerReader::read_children(response.service_value);
    if (fields.size() != 2U || fields[0].tag_class != BerClass::context_specific || fields[0].tag_number != 0 ||
        fields[0].constructed || fields[0].value.size() != 1U || fields[1].encoded_tag != 0xA1U || !fields[1].constructed) {
        throw MmsReportingFormatError("MMS DataSet directory response fields are invalid.");
    }
    const auto definitions = BerReader::read_children(fields[1].value);
    if (definitions.size() > maximum_members) throw MmsReportingFormatError("MMS DataSet directory member count exceeds the configured limit.");
    MmsDataSetDirectoryResponse result;
    result.invoke_id = response.invoke_id;
    result.deletable = fields[0].value[0] != 0U;
    for (const auto& definition : definitions) result.members.push_back(normalize_member(decode_variable_definition(definition)));
    return result;
}

bool MmsInformationReportCodec::is_information_report(const std::span<const std::uint8_t> payload) noexcept {
    MmsInformationReport report;
    return try_decode(payload, report, nullptr);
}

MmsInformationReport MmsInformationReportCodec::decode(const std::span<const std::uint8_t> payload) {
    const auto mms = MmsPduCodec::extract_mms_payload(payload);
    std::size_t offset = 0U;
    BerTlv outer;
    if (!BerReader::try_read_tlv(mms, offset, outer) || offset != mms.size() ||
        outer.tag_class != BerClass::context_specific || outer.tag_number != 3 || !outer.constructed) {
        throw MmsReportingFormatError("MMS PDU is not an Unconfirmed-PDU.");
    }
    const auto unconfirmed = BerReader::read_children(outer.value);
    if (unconfirmed.size() != 1U || unconfirmed[0].tag_class != BerClass::context_specific ||
        unconfirmed[0].tag_number != 0 || !unconfirmed[0].constructed) {
        throw MmsReportingFormatError("MMS Unconfirmed-PDU is not InformationReport.");
    }
    const auto fields = BerReader::read_children(unconfirmed[0].value);
    if (fields.empty() || fields.size() > 2U) throw MmsReportingFormatError("MMS InformationReport field count is invalid.");
    MmsInformationReport report;
    std::size_t result_index = 0U;
    if (fields.size() == 2U) {
        if (fields[0].tag_class != BerClass::context_specific) {
            throw MmsReportingFormatError("MMS InformationReport variable-access specification is invalid.");
        }
        if (fields[0].tag_number == 0 && fields[0].constructed) {
            const auto definitions = BerReader::read_children(fields[0].value);
            if (definitions.size() > maximum_variable_references) throw MmsReportingFormatError("MMS InformationReport variable reference count exceeds the configured limit.");
            for (const auto& definition : definitions) report.variable_references.push_back(decode_variable_definition(definition));
        } else if (fields[0].tag_number == 1) {
            report.variable_references.push_back(
                decode_variable_list_name(fields[0]));
        } else {
            throw MmsReportingFormatError("MMS InformationReport variable-access specification is invalid.");
        }
        result_index = 1U;
    }
    const auto& list = fields[result_index];
    if (list.tag_class != BerClass::context_specific || list.tag_number != 0 || !list.constructed) {
        throw MmsReportingFormatError("MMS InformationReport has no access-result list.");
    }
    const auto results = BerReader::read_children(list.value);
    if (results.empty() || results.size() > maximum_report_items) throw MmsReportingFormatError("MMS InformationReport result count is outside the configured limit.");
    for (std::size_t i = 0U; i < results.size(); ++i) {
        const auto& result = results[i];
        if (result.tag_class == BerClass::context_specific && result.tag_number == 0 && !result.constructed) {
            report.items.push_back({i, std::nullopt, read_u32(result, "MMS InformationReport DataAccessError")});
        } else if (is_mms_data(result)) {
            report.items.push_back({i, MmsDataCodec::decode(result), std::nullopt});
        } else {
            throw MmsReportingFormatError("MMS InformationReport contains an unsupported AccessResult.");
        }
    }
    return report;
}

bool MmsInformationReportCodec::try_decode(
    const std::span<const std::uint8_t> payload, MmsInformationReport& report, std::string* error) noexcept {
    try {
        report = decode(payload);
        if (error) error->clear();
        return true;
    } catch (const std::exception& ex) {
        report = {};
        if (error) *error = ex.what();
        return false;
    }
}

std::vector<std::uint8_t> MmsInformationReportCodec::encode_pdu(const MmsInformationReport& report) {
    if (report.items.empty() || report.items.size() > maximum_report_items ||
        report.variable_references.size() > maximum_variable_references) {
        throw MmsReportingFormatError("MMS InformationReport counts are outside the configured limits.");
    }
    std::vector<std::uint8_t> body;
    if (!report.variable_references.empty()) {
        std::vector<std::uint8_t> definitions;
        for (const auto& reference : report.variable_references) {
            const auto encoded = encode_variable_definition(reference);
            definitions.insert(definitions.end(), encoded.begin(), encoded.end());
        }
        const auto specification = BerWriter::encode_tlv(0xA0U, definitions);
        body.insert(body.end(), specification.begin(), specification.end());
    }
    std::vector<std::uint8_t> results;
    for (const auto& item : report.items) {
        std::vector<std::uint8_t> encoded;
        if (item.value) encoded = MmsDataCodec::encode(*item.value);
        else if (item.failure_code) encoded = BerWriter::encode_tlv(BerClass::context_specific, false, 0, positive_integer_content(*item.failure_code));
        else throw MmsReportingFormatError("MMS InformationReport item has no value or failure code.");
        results.insert(results.end(), encoded.begin(), encoded.end());
    }
    const auto result_list = BerWriter::encode_tlv(0xA0U, results);
    body.insert(body.end(), result_list.begin(), result_list.end());
    const auto information_report = BerWriter::encode_tlv(0xA0U, body);
    return BerWriter::encode_tlv(0xA3U, information_report);
}

std::vector<std::uint8_t> MmsInformationReportCodec::encode_p_data(
    const MmsInformationReport& report, const std::uint32_t context) {
    const auto pdu = encode_pdu(report);
    return MmsPduCodec::wrap_p_data(pdu, context);
}

bool MmsReportBitField::has(const std::string& name) const {
    return std::find(names.begin(), names.end(), name) != names.end();
}

MmsReportHeader MmsReportFrameMapper::decode_header(const MmsInformationReport& report) {
    static constexpr std::array<const char*, 10> option_names{
        "reserved", "sequence-number", "report-time-stamp", "reason-for-inclusion",
        "data-set-name", "data-reference", "buffer-overflow", "entry-id",
        "configuration-revision", "segmentation"};
    if (report.items.size() < 3U) throw MmsReportingFormatError("MMS report does not contain RptID, OptFlds, and inclusion fields.");
    MmsReportHeader header;
    const auto rpt_id = string_value(require_value(report, 0U, "RptID"));
    if (!rpt_id) throw MmsReportingFormatError("MMS report RptID is not a visible string.");
    header.report_id = *rpt_id;
    header.optional_fields = decode_bit_field(require_value(report, 1U, "OptFlds"), option_names);
    std::size_t cursor = 2U;
    if (header.optional_fields.has("sequence-number")) {
        header.sequence_number = unsigned_value(require_value(report, cursor++, "SqNum"));
        if (!header.sequence_number) throw MmsReportingFormatError("MMS report SqNum is not unsigned.");
    }
    if (header.optional_fields.has("report-time-stamp")) header.time_of_entry = require_value(report, cursor++, "TimeOfEntry");
    if (header.optional_fields.has("data-set-name")) {
        const auto value = string_value(require_value(report, cursor++, "DatSet"));
        if (!value) throw MmsReportingFormatError("MMS report DatSet is not a visible string.");
        header.data_set_reference = *value;
    }
    if (header.optional_fields.has("buffer-overflow")) {
        header.buffer_overflow = boolean_value(require_value(report, cursor++, "BufOvfl"));
        if (!header.buffer_overflow) throw MmsReportingFormatError("MMS report BufOvfl is not BOOLEAN.");
    }
    if (header.optional_fields.has("entry-id")) {
        header.entry_id = octets(require_value(report, cursor++, "EntryID"));
        if (header.entry_id.empty()) throw MmsReportingFormatError("MMS report EntryID is not an OCTET STRING.");
    }
    if (header.optional_fields.has("configuration-revision")) {
        header.configuration_revision = unsigned_value(require_value(report, cursor++, "ConfRev"));
        if (!header.configuration_revision) throw MmsReportingFormatError("MMS report ConfRev is not unsigned.");
    }
    if (header.optional_fields.has("segmentation")) {
        header.sub_sequence_number = unsigned_value(require_value(report, cursor++, "SubSqNum"));
        header.more_segments_follow = boolean_value(require_value(report, cursor++, "MoreSegmentsFollow"));
        if (!header.sub_sequence_number || !header.more_segments_follow) throw MmsReportingFormatError("MMS report segmentation fields are invalid.");
    }
    return header;
}

MmsReportFrame MmsReportFrameMapper::map(
    const MmsInformationReport& report, const std::span<const MmsDataSetDirectoryMember> members) {
    static constexpr std::array<const char*, 6> reason_names{
        "data-change", "quality-change", "data-update", "integrity", "general-interrogation", "application-trigger"};
    MmsReportFrame frame;
    frame.header = decode_header(report);
    frame.raw_access_result_count = report.items.size();
    frame.decoder_mode = "opt-fields-exact";
    std::size_t cursor = 2U;
    if (frame.header.optional_fields.has("sequence-number")) ++cursor;
    if (frame.header.optional_fields.has("report-time-stamp")) ++cursor;
    if (frame.header.optional_fields.has("data-set-name")) ++cursor;
    if (frame.header.optional_fields.has("buffer-overflow")) ++cursor;
    if (frame.header.optional_fields.has("entry-id")) ++cursor;
    if (frame.header.optional_fields.has("configuration-revision")) ++cursor;
    if (frame.header.optional_fields.has("segmentation")) cursor += 2U;

    frame.inclusion_item_index = cursor;
    const auto& inclusion = require_value(report, cursor++, "inclusion-bitstring");
    const auto maximum_bits = members.empty() ? MmsInformationReportCodec::maximum_report_items : members.size();
    frame.included_data_set_indexes = bit_indexes(inclusion, maximum_bits);
    if (!members.empty() && std::any_of(frame.included_data_set_indexes.begin(), frame.included_data_set_indexes.end(),
        [members](const std::size_t index) { return index >= members.size(); })) {
        throw MmsReportingFormatError("MMS report inclusion bit references an unknown DataSet member.");
    }

    const auto included_count = frame.included_data_set_indexes.size();
    std::vector<std::string> data_references(included_count);
    if (frame.header.optional_fields.has("data-reference")) {
        for (auto& reference : data_references) {
            const auto value = string_value(require_value(report, cursor++, "data-reference"));
            if (!value) throw MmsReportingFormatError("MMS report data-reference is not a visible string.");
            reference = *value;
        }
    }
    std::vector<MmsInformationReportItem> value_items;
    for (std::size_t i = 0U; i < included_count; ++i) value_items.push_back(require_item(report, cursor++));
    std::vector<MmsReportBitField> reasons(included_count);
    if (frame.header.optional_fields.has("reason-for-inclusion")) {
        for (auto& reason : reasons) reason = decode_bit_field(require_value(report, cursor++, "reason-for-inclusion"), reason_names);
    }
    if (cursor != report.items.size()) throw MmsReportingFormatError("MMS report contains trailing access results after exact decoding.");

    for (std::size_t i = 0U; i < included_count; ++i) {
        MmsReportValue value;
        value.data_set_index = frame.included_data_set_indexes[i];
        if (!members.empty()) value.member = members[value.data_set_index];
        value.value = value_items[i].value;
        value.failure_code = value_items[i].failure_code;
        value.data_reference = data_references[i];
        value.reason_for_inclusion = std::move(reasons[i]);
        frame.values.push_back(std::move(value));
    }
    return frame;
}

std::string MmsReportFrame::routing_key() const {
    return header.report_id;
}

MmsReadRequest MmsReportControlStateMapper::build_read_request(
    const std::uint32_t invoke_id, const MmsReportControlCandidate& candidate,
    const std::span<const std::string> attributes) {
    if (attributes.empty() || attributes.size() > MmsServiceCodec::maximum_variables) {
        throw MmsReportingFormatError("RCB attribute read count is outside the configured limit.");
    }
    MmsReadRequest request;
    request.invoke_id = invoke_id;
    for (const auto& attribute : attributes) request.variables.push_back(candidate.attribute_object_name(attribute));
    return request;
}

MmsReportControlState MmsReportControlStateMapper::map_read_response(
    const MmsReportControlCandidate& candidate, const std::span<const std::string> attributes,
    const MmsReadResponse& response, const MmsDataSetDirectoryResponse* directory, const bool caller_owned) {
    if (attributes.size() != response.results.size()) throw MmsReportingFormatError("RCB attribute/result counts do not match.");
    MmsReportControlState state;
    state.candidate = candidate;
    for (std::size_t i = 0U; i < attributes.size(); ++i) {
        const auto& attribute = attributes[i];
        const auto& result = response.results[i];
        if (!result.value) {
            state.diagnostics.push_back(attribute + " read failed" + (result.failure_code ? " (" + std::to_string(*result.failure_code) + ")" : "."));
            continue;
        }
        const auto& value = *result.value;
        if (attribute == "DatSet") state.data_set_reference = string_value(value).value_or("");
        else if (attribute == "RptID") state.report_id = string_value(value).value_or("");
        else if (attribute == "ConfRev") state.configuration_revision = unsigned_value(value);
        else if (attribute == "IntgPd") state.integrity_period_ms = unsigned_value(value);
        else if (attribute == "BufTm") state.buffer_time_ms = unsigned_value(value);
        else if (attribute == "SqNum") state.sequence_number = unsigned_value(value);
        else if (attribute == "RptEna") state.report_enabled = boolean_value(value);
        else if (attribute == "Resv") state.reserved = boolean_value(value);
        else if (attribute == "ResvTms") state.reservation_time_seconds = unsigned_value(value);
        else if (attribute == "Owner") state.owner = octets(value);
        else if (attribute == "EntryID") state.entry_id = octets(value);
        else if (attribute == "TimeOfEntry") state.time_of_entry = value;
        else if (attribute == "TrgOps") {
            static constexpr std::array<const char*, 6> names{"data-change", "quality-change", "data-update", "integrity", "general-interrogation", "application-trigger"};
            state.trigger_options = decode_bit_field(value, names);
        } else if (attribute == "OptFlds") {
            static constexpr std::array<const char*, 10> names{"reserved", "sequence-number", "report-time-stamp", "reason-for-inclusion", "data-set-name", "data-reference", "buffer-overflow", "entry-id", "configuration-revision", "segmentation"};
            state.optional_fields = decode_bit_field(value, names);
        }
    }

    if (state.data_set_reference.empty()) {
        state.availability = MmsRcbAvailability::no_data_set;
        state.availability_confidence = MmsRcbAvailabilityConfidence::exact;
        state.availability_reason = "RCB DatSet is empty.";
    } else if (directory == nullptr) {
        state.availability = MmsRcbAvailability::data_set_unreadable;
        state.availability_confidence = MmsRcbAvailabilityConfidence::reduced;
        state.availability_reason = "DataSet directory was not supplied.";
    } else if (directory->members.empty()) {
        state.availability = MmsRcbAvailability::data_set_empty;
        state.availability_confidence = MmsRcbAvailabilityConfidence::exact;
        state.availability_reason = "DataSet directory is empty.";
    } else if (caller_owned) {
        state.availability = MmsRcbAvailability::used_by_caller;
        state.availability_confidence = MmsRcbAvailabilityConfidence::exact;
        state.availability_reason = "Caller ownership was supplied by the offline evidence source.";
    } else if (state.report_enabled.value_or(false) || state.reserved.value_or(false) || !state.owner.empty()) {
        state.availability = MmsRcbAvailability::in_use;
        state.availability_confidence = candidate.buffered && !state.reserved && state.owner.empty()
            ? MmsRcbAvailabilityConfidence::reduced : MmsRcbAvailabilityConfidence::exact;
        state.availability_reason = "RCB enable, reservation, or owner evidence indicates active use.";
    } else {
        state.availability = MmsRcbAvailability::available;
        state.availability_confidence = candidate.buffered
            ? MmsRcbAvailabilityConfidence::reduced : MmsRcbAvailabilityConfidence::exact;
        state.availability_reason = candidate.buffered
            ? "No active-use evidence was observed; BRCB reservation evidence may be incomplete."
            : "RCB is disabled and not reserved.";
    }
    return state;
}

MmsReportObservation MmsReportSequenceTracker::observe(const MmsReportFrame& frame) {
    const auto key = frame.routing_key();
    if (key.empty()) throw MmsReportingFormatError("MMS report routing key is empty.");
    if (!streams_.contains(key) && streams_.size() >= maximum_streams) {
        throw MmsReportingFormatError("MMS report sequence tracker stream limit exceeded.");
    }
    auto& state = streams_[key];
    const bool first = state.report_count == 0U;
    if (first) state.key = key;
    std::vector<MmsReportContinuityEvent> events;
    if (first) add_event(events, MmsReportContinuityEventKind::first_report);

    if (frame.header.sequence_number) {
        const auto current = *frame.header.sequence_number;
        if (!first && state.last_sequence_number) {
            const auto previous = *state.last_sequence_number;
            if (current == previous) {
                ++state.duplicate_count;
                add_event(events, MmsReportContinuityEventKind::duplicate);
            } else if (current == previous + 1U) {
                add_event(events, MmsReportContinuityEventKind::in_order);
            } else if (previous >= 65'000U && current < 1'000U) {
                ++state.wrap_count;
                add_event(events, MmsReportContinuityEventKind::sequence_wrap);
            } else if (current < previous) {
                ++state.reset_count;
                add_event(events, MmsReportContinuityEventKind::sequence_reset);
            } else {
                ++state.gap_count;
                state.missing_sequence_count += current - previous - 1U;
                add_event(events, MmsReportContinuityEventKind::sequence_gap);
            }
        }
        state.last_sequence_number = current;
    }

    if (frame.header.configuration_revision) {
        if (state.last_configuration_revision && *state.last_configuration_revision != *frame.header.configuration_revision) {
            ++state.configuration_change_count;
            add_event(events, MmsReportContinuityEventKind::configuration_revision_changed);
        }
        state.last_configuration_revision = frame.header.configuration_revision;
    }
    if (!frame.header.data_set_reference.empty()) {
        if (!state.last_data_set_reference.empty() && state.last_data_set_reference != frame.header.data_set_reference) {
            ++state.data_set_change_count;
            add_event(events, MmsReportContinuityEventKind::data_set_changed);
        }
        state.last_data_set_reference = frame.header.data_set_reference;
    }
    if (frame.header.buffer_overflow.value_or(false)) {
        ++state.buffer_overflow_count;
        add_event(events, MmsReportContinuityEventKind::buffer_overflow);
    }

    if (frame.header.sub_sequence_number && frame.header.more_segments_follow) {
        const auto current = *frame.header.sub_sequence_number;
        if (!state.segmentation_open) {
            state.segmentation_open = *frame.header.more_segments_follow;
            add_event(events, *frame.header.more_segments_follow
                ? MmsReportContinuityEventKind::segmentation_started
                : MmsReportContinuityEventKind::segmentation_completed);
        } else {
            if (state.last_sub_sequence_number && current != *state.last_sub_sequence_number + 1U) {
                ++state.segmentation_gap_count;
                add_event(events, MmsReportContinuityEventKind::segmentation_gap);
            }
            if (*frame.header.more_segments_follow) add_event(events, MmsReportContinuityEventKind::segmentation_continued);
            else {
                state.segmentation_open = false;
                add_event(events, MmsReportContinuityEventKind::segmentation_completed);
            }
        }
        state.last_sub_sequence_number = current;
    } else if (state.segmentation_open) {
        ++state.segmentation_gap_count;
        state.segmentation_open = false;
        add_event(events, MmsReportContinuityEventKind::segmentation_gap);
    }

    ++state.report_count;
    state.value_count += frame.values.size();
    state.last_entry_id = frame.header.entry_id;
    return {state, std::move(events)};
}

void MmsReportSequenceTracker::erase(const std::string& key) { streams_.erase(key); }
void MmsReportSequenceTracker::clear() { streams_.clear(); }

MmsOfflineReportMonitor::MmsOfflineReportMonitor(MmsOfflineReportMonitorOptions options) : options_(options) {
    if (options_.maximum_streams == 0U || options_.maximum_streams > 4'096U ||
        options_.maximum_frames_per_stream == 0U || options_.maximum_frames_per_stream > 65'536U) {
        throw MmsReportingFormatError("Offline report monitor limits are invalid.");
    }
}

MmsReportObservation MmsOfflineReportMonitor::ingest(const MmsReportFrame& frame) {
    auto observation = tracker_.observe(frame);
    const auto key = frame.routing_key();
    auto& record = streams_[key];
    record.state = observation.state;
    record.frames.push_back(frame);
    while (record.frames.size() > options_.maximum_frames_per_stream) record.frames.pop_front();
    record.touch_order = ++touch_counter_;
    enforce_stream_limit();
    return observation;
}

std::vector<MmsOfflineReportStreamSnapshot> MmsOfflineReportMonitor::snapshots() const {
    std::vector<MmsOfflineReportStreamSnapshot> result;
    result.reserve(streams_.size());
    for (const auto& [_, record] : streams_) {
        result.push_back({record.state, {record.frames.begin(), record.frames.end()}});
    }
    return result;
}

void MmsOfflineReportMonitor::clear() {
    streams_.clear();
    tracker_.clear();
    touch_counter_ = 0U;
}

void MmsOfflineReportMonitor::enforce_stream_limit() {
    while (streams_.size() > options_.maximum_streams) {
        const auto victim = std::min_element(streams_.begin(), streams_.end(), [](const auto& left, const auto& right) {
            return left.second.touch_order < right.second.touch_order;
        });
        if (victim == streams_.end()) return;
        tracker_.erase(victim->first);
        streams_.erase(victim);
    }
}

} // namespace ar::iec61850::mms
