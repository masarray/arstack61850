// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/services.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ar::iec61850::mms {

class MmsReportingFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct MmsDiscoverySnapshot final {
    std::map<std::string, std::vector<std::string>, std::less<>> domain_variables;
    std::map<std::string, std::vector<std::string>, std::less<>> domain_variable_lists;
};

struct MmsDataSetCandidate final {
    std::string domain;
    std::string logical_node;
    std::string name;
    std::string reference;
    std::string raw_mms_name;

    friend bool operator==(const MmsDataSetCandidate&, const MmsDataSetCandidate&) = default;
};

struct MmsReportControlCandidate final {
    std::string domain;
    std::string logical_node;
    std::string functional_constraint;
    std::string name;
    std::string reference;
    bool buffered{};
    std::vector<std::string> attributes;
    std::vector<std::string> diagnostics;

    [[nodiscard]] std::string mode() const;
    [[nodiscard]] MmsObjectName attribute_object_name(std::string attribute) const;

    friend bool operator==(const MmsReportControlCandidate&,
                           const MmsReportControlCandidate&) = default;
};

struct MmsReportInventory final {
    std::vector<MmsDataSetCandidate> data_sets;
    std::vector<MmsReportControlCandidate> report_controls;

    [[nodiscard]] std::size_t buffered_count() const noexcept;
    [[nodiscard]] std::size_t unbuffered_count() const noexcept;
};

class MmsReportInventoryBuilder final {
public:
    static constexpr std::size_t maximum_domains = 4'096U;
    static constexpr std::size_t maximum_names_per_domain = 65'536U;
    static constexpr std::size_t maximum_inventory_items = 262'144U;

    [[nodiscard]] static MmsReportInventory build(const MmsDiscoverySnapshot& snapshot);
};

struct MmsDataSetDirectoryMember final {
    MmsObjectName object_name;
    std::string mms_reference;
    std::string user_reference;
    std::string functional_constraint;
    std::string logical_node;
    std::string data_object_path;
    std::uint32_t confidence{};

    friend bool operator==(const MmsDataSetDirectoryMember&,
                           const MmsDataSetDirectoryMember&) = default;
};

struct MmsDataSetDirectoryRequest final {
    std::uint32_t invoke_id{};
    MmsObjectName data_set_name;

    friend bool operator==(const MmsDataSetDirectoryRequest&,
                           const MmsDataSetDirectoryRequest&) = default;
};

struct MmsDataSetDirectoryResponse final {
    std::uint32_t invoke_id{};
    bool deletable{};
    std::vector<MmsDataSetDirectoryMember> members;

    friend bool operator==(const MmsDataSetDirectoryResponse&,
                           const MmsDataSetDirectoryResponse&) = default;
};

class MmsDataSetDirectoryCodec final {
public:
    static constexpr std::size_t maximum_members = 65'536U;

    [[nodiscard]] static MmsObjectName parse_data_set_reference(
        const std::string& reference);
    [[nodiscard]] static std::string to_iec_reference(const MmsObjectName& name);

    [[nodiscard]] static std::vector<std::uint8_t> encode_request_pdu(
        const MmsDataSetDirectoryRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_request_p_data(
        const MmsDataSetDirectoryRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsDataSetDirectoryRequest decode_request(
        std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] static std::vector<std::uint8_t> encode_response_pdu(
        const MmsDataSetDirectoryResponse& response);
    [[nodiscard]] static std::vector<std::uint8_t> encode_response_p_data(
        const MmsDataSetDirectoryResponse& response,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsDataSetDirectoryResponse decode_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::optional<std::uint32_t> expected_invoke_id = std::nullopt);
};

struct MmsInformationReportItem final {
    std::size_t index{};
    std::optional<MmsDataValue> value;
    std::optional<std::uint32_t> failure_code;
};

struct MmsInformationReport final {
    std::vector<MmsObjectName> variable_references;
    std::vector<MmsInformationReportItem> items;
};

class MmsInformationReportCodec final {
public:
    static constexpr std::size_t maximum_report_items = 65'536U;
    static constexpr std::size_t maximum_variable_references = 65'536U;

    [[nodiscard]] static bool is_information_report(
        std::span<const std::uint8_t> presentation_or_mms_payload) noexcept;
    [[nodiscard]] static MmsInformationReport decode(
        std::span<const std::uint8_t> presentation_or_mms_payload);
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        MmsInformationReport& report,
        std::string* error = nullptr) noexcept;
    [[nodiscard]] static std::vector<std::uint8_t> encode_pdu(
        const MmsInformationReport& report);
    [[nodiscard]] static std::vector<std::uint8_t> encode_p_data(
        const MmsInformationReport& report,
        std::uint32_t presentation_context_id = 3U);
};

struct MmsReportBitField final {
    std::vector<std::uint8_t> raw;
    std::vector<std::size_t> set_bit_indexes;
    std::vector<std::string> names;

    [[nodiscard]] bool has(const std::string& name) const;
};

struct MmsReportHeader final {
    std::string report_id;
    MmsReportBitField optional_fields;
    std::optional<std::uint64_t> sequence_number;
    std::optional<std::uint64_t> sub_sequence_number;
    std::optional<bool> more_segments_follow;
    std::optional<MmsDataValue> time_of_entry;
    std::string data_set_reference;
    std::optional<bool> buffer_overflow;
    std::vector<std::uint8_t> entry_id;
    std::optional<std::uint64_t> configuration_revision;
};

struct MmsReportValue final {
    std::size_t data_set_index{};
    std::optional<MmsDataSetDirectoryMember> member;
    std::optional<MmsDataValue> value;
    std::optional<std::uint32_t> failure_code;
    std::string data_reference;
    MmsReportBitField reason_for_inclusion;
};

struct MmsReportFrame final {
    MmsReportHeader header;
    std::vector<MmsReportValue> values;
    std::vector<std::size_t> included_data_set_indexes;
    std::optional<std::size_t> inclusion_item_index;
    std::size_t raw_access_result_count{};
    std::string decoder_mode;
    std::vector<std::string> warnings;

    [[nodiscard]] std::string routing_key() const;
};

class MmsReportFrameMapper final {
public:
    [[nodiscard]] static MmsReportHeader decode_header(
        const MmsInformationReport& report);
    [[nodiscard]] static MmsReportFrame map(
        const MmsInformationReport& report,
        std::span<const MmsDataSetDirectoryMember> members);
};

enum class MmsRcbAvailability : std::uint8_t {
    unknown,
    available,
    in_use,
    used_by_caller,
    no_data_set,
    data_set_unreadable,
    data_set_empty,
};

enum class MmsRcbAvailabilityConfidence : std::uint8_t {
    unknown,
    reduced,
    exact,
};

struct MmsReportControlState final {
    MmsReportControlCandidate candidate;
    std::string data_set_reference;
    std::string report_id;
    std::optional<std::uint64_t> configuration_revision;
    std::optional<std::uint64_t> integrity_period_ms;
    std::optional<std::uint64_t> buffer_time_ms;
    std::optional<std::uint64_t> sequence_number;
    std::optional<bool> report_enabled;
    std::optional<bool> reserved;
    std::optional<std::uint64_t> reservation_time_seconds;
    std::vector<std::uint8_t> owner;
    MmsReportBitField trigger_options;
    MmsReportBitField optional_fields;
    std::vector<std::uint8_t> entry_id;
    std::optional<MmsDataValue> time_of_entry;
    std::vector<std::string> diagnostics;
    MmsRcbAvailability availability{MmsRcbAvailability::unknown};
    MmsRcbAvailabilityConfidence availability_confidence{
        MmsRcbAvailabilityConfidence::unknown};
    std::string availability_reason;
};

class MmsReportControlStateMapper final {
public:
    [[nodiscard]] static MmsReadRequest build_read_request(
        std::uint32_t invoke_id,
        const MmsReportControlCandidate& candidate,
        std::span<const std::string> attributes);

    [[nodiscard]] static MmsReportControlState map_read_response(
        const MmsReportControlCandidate& candidate,
        std::span<const std::string> attributes,
        const MmsReadResponse& response,
        const MmsDataSetDirectoryResponse* directory = nullptr,
        bool caller_owned = false);
};

enum class MmsReportContinuityEventKind : std::uint8_t {
    first_report,
    in_order,
    duplicate,
    sequence_gap,
    sequence_wrap,
    sequence_reset,
    configuration_revision_changed,
    data_set_changed,
    buffer_overflow,
    segmentation_started,
    segmentation_continued,
    segmentation_completed,
    segmentation_gap,
};

struct MmsReportContinuityEvent final {
    MmsReportContinuityEventKind kind{MmsReportContinuityEventKind::first_report};
    std::string message;
};

struct MmsReportStreamState final {
    std::string key;
    std::uint64_t report_count{};
    std::uint64_t value_count{};
    std::uint64_t duplicate_count{};
    std::uint64_t gap_count{};
    std::uint64_t missing_sequence_count{};
    std::uint64_t wrap_count{};
    std::uint64_t reset_count{};
    std::uint64_t configuration_change_count{};
    std::uint64_t data_set_change_count{};
    std::uint64_t buffer_overflow_count{};
    std::uint64_t segmentation_gap_count{};
    std::optional<std::uint64_t> last_sequence_number;
    std::optional<std::uint64_t> last_sub_sequence_number;
    std::optional<std::uint64_t> last_configuration_revision;
    std::string last_data_set_reference;
    std::vector<std::uint8_t> last_entry_id;
    bool segmentation_open{};
};

struct MmsReportObservation final {
    MmsReportStreamState state;
    std::vector<MmsReportContinuityEvent> events;
};

class MmsReportSequenceTracker final {
public:
    [[nodiscard]] MmsReportObservation observe(const MmsReportFrame& frame);
    void erase(const std::string& key);
    void clear();

private:
    static constexpr std::size_t maximum_streams = 4'096U;
    std::map<std::string, MmsReportStreamState, std::less<>> streams_;
};

struct MmsOfflineReportMonitorOptions final {
    std::size_t maximum_streams{128U};
    std::size_t maximum_frames_per_stream{256U};
};

struct MmsOfflineReportStreamSnapshot final {
    MmsReportStreamState state;
    std::vector<MmsReportFrame> recent_frames;
};

class MmsOfflineReportMonitor final {
public:
    explicit MmsOfflineReportMonitor(MmsOfflineReportMonitorOptions options = {});

    [[nodiscard]] MmsReportObservation ingest(const MmsReportFrame& frame);
    [[nodiscard]] std::vector<MmsOfflineReportStreamSnapshot> snapshots() const;
    void clear();

private:
    struct StreamRecord final {
        MmsReportStreamState state;
        std::deque<MmsReportFrame> frames;
        std::uint64_t touch_order{};
    };

    void enforce_stream_limit();

    MmsOfflineReportMonitorOptions options_;
    MmsReportSequenceTracker tracker_;
    std::map<std::string, StreamRecord, std::less<>> streams_;
    std::uint64_t touch_counter_{};
};

} // namespace ar::iec61850::mms
