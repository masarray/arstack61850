// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/reporting.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

class MmsDynamicDataSetError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct MmsDefineNamedVariableListRequest final {
    std::uint32_t invoke_id{};
    MmsObjectName data_set_name;
    std::vector<MmsObjectName> members;

    friend bool operator==(const MmsDefineNamedVariableListRequest&,
                           const MmsDefineNamedVariableListRequest&) = default;
};

struct MmsDefineNamedVariableListResponse final {
    std::uint32_t invoke_id{};

    friend bool operator==(const MmsDefineNamedVariableListResponse&,
                           const MmsDefineNamedVariableListResponse&) = default;
};

struct MmsDeleteNamedVariableListRequest final {
    std::uint32_t invoke_id{};
    MmsObjectName data_set_name;

    friend bool operator==(const MmsDeleteNamedVariableListRequest&,
                           const MmsDeleteNamedVariableListRequest&) = default;
};

struct MmsDeleteNamedVariableListResponse final {
    std::uint32_t invoke_id{};
    std::optional<std::uint32_t> number_matched;
    std::optional<std::uint32_t> number_deleted;

    [[nodiscard]] bool deleted() const noexcept {
        return number_deleted.value_or(0U) > 0U;
    }

    friend bool operator==(const MmsDeleteNamedVariableListResponse&,
                           const MmsDeleteNamedVariableListResponse&) = default;
};

class MmsNamedVariableListCodec final {
public:
    static constexpr std::int32_t define_service_tag = 11;
    static constexpr std::int32_t delete_service_tag = 13;
    static constexpr std::size_t maximum_members = 65'536U;

    [[nodiscard]] static std::vector<std::uint8_t> encode_define_request_pdu(
        const MmsDefineNamedVariableListRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_define_request_p_data(
        const MmsDefineNamedVariableListRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsDefineNamedVariableListRequest decode_define_request(
        std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] static std::vector<std::uint8_t> encode_define_response_pdu(
        const MmsDefineNamedVariableListResponse& response);
    [[nodiscard]] static std::vector<std::uint8_t> encode_define_response_p_data(
        const MmsDefineNamedVariableListResponse& response,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsDefineNamedVariableListResponse decode_define_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::optional<std::uint32_t> expected_invoke_id = std::nullopt);

    [[nodiscard]] static std::vector<std::uint8_t> encode_delete_request_pdu(
        const MmsDeleteNamedVariableListRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_delete_request_p_data(
        const MmsDeleteNamedVariableListRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsDeleteNamedVariableListRequest decode_delete_request(
        std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] static std::vector<std::uint8_t> encode_delete_response_pdu(
        const MmsDeleteNamedVariableListResponse& response);
    [[nodiscard]] static std::vector<std::uint8_t> encode_delete_response_p_data(
        const MmsDeleteNamedVariableListResponse& response,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsDeleteNamedVariableListResponse decode_delete_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::optional<std::uint32_t> expected_invoke_id = std::nullopt);
};

struct MmsDynamicDataSetOptions final {
    // Dynamic report DataSets are safest when intentionally small. The wire codec
    // supports the full bounded MMS list size, while this operational default can
    // be raised explicitly by a caller that has verified the target IED limits.
    std::size_t maximum_members{64U};
    bool verify_after_create{true};
};

struct MmsDynamicDataSetCreateResult final {
    std::string data_set_reference;
    MmsDefineNamedVariableListResponse define_response;
    std::optional<MmsDataSetDirectoryResponse> verified_directory;
};

enum class MmsDynamicDataSetDeletePolicy : std::uint8_t {
    owned_only,
    explicit_override,
};

class MmsDynamicDataSetRuntime final {
public:
    explicit MmsDynamicDataSetRuntime(
        MmsAssociationRuntime& association,
        MmsDynamicDataSetOptions options = {});

    [[nodiscard]] MmsDynamicDataSetCreateResult create(
        const std::string& data_set_reference,
        std::span<const MmsObjectName> members,
        std::stop_token stop_token = {});

    [[nodiscard]] MmsDeleteNamedVariableListResponse remove(
        const std::string& data_set_reference,
        MmsDynamicDataSetDeletePolicy policy = MmsDynamicDataSetDeletePolicy::owned_only,
        std::stop_token stop_token = {});

    [[nodiscard]] bool owns(const std::string& data_set_reference) const;
    [[nodiscard]] std::vector<std::string> owned_data_sets() const;

private:
    [[nodiscard]] std::string normalize_reference(const std::string& reference) const;
    [[nodiscard]] MmsDeleteNamedVariableListResponse remove_impl(
        const std::string& normalized_reference,
        std::stop_token stop_token);
    [[nodiscard]] MmsDataSetDirectoryResponse verify(
        const MmsObjectName& data_set_name,
        std::stop_token stop_token);
    void synchronize_ownership_scope() const;
    void capture_ownership_scope() const;
    void clear_ownership_scope() const noexcept;
    void require_request_within_negotiated_limit(
        std::span<const std::uint8_t> mms_pdu,
        const char* operation) const;

    MmsAssociationRuntime& association_;
    MmsDynamicDataSetOptions options_{};
    mutable std::set<std::string, std::less<>> owned_data_sets_;
    mutable bool ownership_scope_valid_{};
    mutable MmsEndpoint ownership_endpoint_{};
    mutable std::size_t ownership_event_count_{};
    mutable MmsAssociationEventKind ownership_marker_kind_{
        MmsAssociationEventKind::state_changed};
    mutable MmsAssociationRuntimeState ownership_marker_state_{
        MmsAssociationRuntimeState::disconnected};
    mutable std::optional<std::uint32_t> ownership_marker_invoke_id_;
    mutable std::string ownership_marker_message_;
};

} // namespace ar::iec61850::mms
