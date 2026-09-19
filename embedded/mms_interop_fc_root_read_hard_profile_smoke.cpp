// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::string_view kDomain{"AA1E1F06R4ADD"};
constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kTrue{0x83U, 0x01U, 0xFFU};

// Exact MMS Confirmed-Request PDU captured from vendor external IEC 61850 client SCL-connect
// discovery against the known-good ARIEC61850 simulator. Invoke 2 reads six
// Functional Constraint roots in AA1E1F06R4ADD:
// LLN0$CF, LLN0$DC, LLN0$EX, LLN0$RP, LLN0$SP, LLN0$ST.
constexpr std::array<std::uint8_t, 198U> kIedScoutInvoke2{
    0xA0U, 0x81U, 0xC3U, 0x02U, 0x01U, 0x02U, 0xA4U, 0x81U, 0xBDU, 0x80U, 0x01U, 0x00U,
    0xA1U, 0x81U, 0xB7U, 0xA0U, 0x81U, 0xB4U, 0x30U, 0x1CU, 0xA0U, 0x1AU, 0xA1U, 0x18U,
    0x1AU, 0x0DU, 0x41U, 0x41U, 0x31U, 0x45U, 0x31U, 0x46U, 0x30U, 0x36U, 0x52U, 0x34U,
    0x41U, 0x44U, 0x44U, 0x1AU, 0x07U, 0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U, 0x43U, 0x46U,
    0x30U, 0x1CU, 0xA0U, 0x1AU, 0xA1U, 0x18U, 0x1AU, 0x0DU, 0x41U, 0x41U, 0x31U, 0x45U,
    0x31U, 0x46U, 0x30U, 0x36U, 0x52U, 0x34U, 0x41U, 0x44U, 0x44U, 0x1AU, 0x07U, 0x4CU,
    0x4CU, 0x4EU, 0x30U, 0x24U, 0x44U, 0x43U, 0x30U, 0x1CU, 0xA0U, 0x1AU, 0xA1U, 0x18U,
    0x1AU, 0x0DU, 0x41U, 0x41U, 0x31U, 0x45U, 0x31U, 0x46U, 0x30U, 0x36U, 0x52U, 0x34U,
    0x41U, 0x44U, 0x44U, 0x1AU, 0x07U, 0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U, 0x45U, 0x58U,
    0x30U, 0x1CU, 0xA0U, 0x1AU, 0xA1U, 0x18U, 0x1AU, 0x0DU, 0x41U, 0x41U, 0x31U, 0x45U,
    0x31U, 0x46U, 0x30U, 0x36U, 0x52U, 0x34U, 0x41U, 0x44U, 0x44U, 0x1AU, 0x07U, 0x4CU,
    0x4CU, 0x4EU, 0x30U, 0x24U, 0x52U, 0x50U, 0x30U, 0x1CU, 0xA0U, 0x1AU, 0xA1U, 0x18U,
    0x1AU, 0x0DU, 0x41U, 0x41U, 0x31U, 0x45U, 0x31U, 0x46U, 0x30U, 0x36U, 0x52U, 0x34U,
    0x41U, 0x44U, 0x44U, 0x1AU, 0x07U, 0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U, 0x53U, 0x50U,
    0x30U, 0x1CU, 0xA0U, 0x1AU, 0xA1U, 0x18U, 0x1AU, 0x0DU, 0x41U, 0x41U, 0x31U, 0x45U,
    0x31U, 0x46U, 0x30U, 0x36U, 0x52U, 0x34U, 0x41U, 0x44U, 0x44U, 0x1AU, 0x07U, 0x4CU,
    0x4CU, 0x4EU, 0x30U, 0x24U, 0x53U, 0x54U};

[[nodiscard]] wire::EncodeResult read_true(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, kTrue.size()};
    }
    if (destination.size() < kTrue.size()) {
        return {wire::EncodeStatus::buffer_too_small, 0U, kTrue.size()};
    }
    for (std::size_t index = 0U; index < kTrue.size(); ++index) {
        destination[index] = kTrue[index];
    }
    return {wire::EncodeStatus::ok, kTrue.size(), kTrue.size()};
}

[[nodiscard]] bool is_structure(
    const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView value;
    return asn1::BerSpanReader::try_read_exact(encoded, value) &&
        value.tag_class == asn1::BerClass::context_specific &&
        value.tag_number == 2 && value.constructed;
}

} // namespace

int main() {
    const bool context = true;
    // Deliberately scramble the RP leaf order to mirror production composition:
    // URCB/BRCB banks append per-association objects after the base model. The
    // synthesis algorithm must not require contiguous or pre-sorted descendants.
    const std::array<mms::MmsStaticObjectEntry, 8U> entries{
        mms::MmsStaticObjectEntry{kDomain, "LLN0$RP$A_URCB$RptID", kBooleanType, read_true, &context},
        mms::MmsStaticObjectEntry{kDomain, "LLN0$CF$Mod$ctlModel", kBooleanType, read_true, &context},
        mms::MmsStaticObjectEntry{kDomain, "LLN0$ST$Mod$stVal", kBooleanType, read_true, &context},
        mms::MmsStaticObjectEntry{kDomain, "LLN0$DC$NamPlt$vendor", kBooleanType, read_true, &context},
        mms::MmsStaticObjectEntry{kDomain, "LLN0$RP$A_URCB_1$RptID", kBooleanType, read_true, &context},
        mms::MmsStaticObjectEntry{kDomain, "LLN0$EX$NamPlt$ldNs", kBooleanType, read_true, &context},
        mms::MmsStaticObjectEntry{kDomain, "LLN0$RP$A_URCB$RptEna", kBooleanType, read_true, &context},
        mms::MmsStaticObjectEntry{kDomain, "LLN0$SP$Some$setVal", kBooleanType, read_true, &context}};

    const mms::MmsStaticObjectTable table{entries};
    if (!table.valid()) return 1;

    const mms::MmsStaticApplicationDispatcher dispatcher{table};
    std::array<std::uint8_t, 8'192U> response{};
    std::array<std::uint8_t, 8'192U> workspace{};
    const auto dispatched = dispatcher.dispatch(kIedScoutInvoke2, response, workspace);
    if (!dispatched.success() ||
        dispatched.service != mms::MmsWireConfirmedService::read ||
        dispatched.invoke_id != 2U || dispatched.bytes_written == 0U) {
        return 2;
    }

    mms::MmsReadResponseView decoded;
    if (!mms::MmsServiceSpanCodec::try_decode_read_response(
            std::span<const std::uint8_t>{response}.first(dispatched.bytes_written),
            decoded) ||
        decoded.invoke_id != 2U || decoded.result_count != 6U) {
        return 3;
    }

    for (std::size_t index = 0U; index < decoded.result_count; ++index) {
        mms::MmsReadAccessResultView result;
        if (!decoded.try_result(index, result) || !result.success ||
            !is_structure(result.encoded_data)) {
            return static_cast<int>(10U + index);
        }
    }

    // RP is the fourth requested FC root. It must be a nested structure rather
    // than a flat leaf or object-non-existent failure.
    mms::MmsReadAccessResultView rp;
    if (!decoded.try_result(3U, rp) || !rp.success || !is_structure(rp.encoded_data)) {
        return 30;
    }
    asn1::BerTlvView rp_outer;
    asn1::BerTlvView first_rcb;
    std::size_t offset = 0U;
    if (!asn1::BerSpanReader::try_read_exact(rp.encoded_data, rp_outer) ||
        !asn1::BerSpanReader::try_read_tlv(rp_outer.value, offset, first_rcb) ||
        first_rcb.tag_class != asn1::BerClass::context_specific ||
        first_rcb.tag_number != 2 || !first_rcb.constructed) {
        return 31;
    }

    return 0;
}
