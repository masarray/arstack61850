// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

enum class MmsFileFailureKind : std::uint8_t {
    none,
    invalid_argument,
    malformed_response,
    invoke_id_mismatch,
    unexpected_service,
    confirmed_error,
    reject_or_abort,
    transport,
    cancelled,
    timed_out,
    protocol,
    limit_exceeded,
    sink,
    cleanup,
};

struct MmsFileDiagnostic final {
    std::string stage;
    MmsFileFailureKind failure_kind{MmsFileFailureKind::none};
    bool success{};
    std::string message;
    std::optional<std::uint32_t> invoke_id;
    std::optional<std::int32_t> frsm_id;
    std::optional<std::int32_t> error_class;
    std::optional<std::uint32_t> error_value;
    std::string request_hex;
    std::string response_hex;

    [[nodiscard]] bool file_non_existent() const noexcept {
        return failure_kind == MmsFileFailureKind::confirmed_error &&
               error_class == 11 && error_value == 7U;
    }
};

class MmsFileServiceError final : public std::runtime_error {
public:
    explicit MmsFileServiceError(MmsFileDiagnostic diagnostic);

    [[nodiscard]] const MmsFileDiagnostic& diagnostic() const noexcept {
        return diagnostic_;
    }

private:
    MmsFileDiagnostic diagnostic_;
};

struct MmsFileDirectoryEntry final {
    std::string name;
    std::string path;
    std::optional<std::uint32_t> size_bytes;
    std::vector<std::uint8_t> last_modified;

    [[nodiscard]] bool likely_directory() const;
    friend bool operator==(const MmsFileDirectoryEntry&,
                           const MmsFileDirectoryEntry&) = default;
};

struct MmsFileDirectoryRequest final {
    std::uint32_t invoke_id{};
    std::string directory_name;
    std::string continue_after;
};

struct MmsFileDirectoryResponse final {
    std::uint32_t invoke_id{};
    std::vector<MmsFileDirectoryEntry> entries;
    bool more_follows{};
};

struct MmsFileOpenRequest final {
    std::uint32_t invoke_id{};
    std::string remote_path;
    std::uint32_t initial_position{};
    bool rooted_backslash{};
};

struct MmsFileOpenResponse final {
    std::uint32_t invoke_id{};
    std::int32_t frsm_id{};
    std::optional<std::uint32_t> file_size_bytes;
    std::vector<std::uint8_t> last_modified;
};

struct MmsFileReadRequest final {
    std::uint32_t invoke_id{};
    std::int32_t frsm_id{};
};

struct MmsFileReadResponse final {
    std::uint32_t invoke_id{};
    std::vector<std::uint8_t> data;
    bool more_follows{true};
};

struct MmsFileCloseRequest final {
    std::uint32_t invoke_id{};
    std::int32_t frsm_id{};
};

struct MmsFileCloseResponse final {
    std::uint32_t invoke_id{};
};

class MmsFileServiceCodec final {
public:
    static constexpr std::int32_t file_open_service_tag = 72;
    static constexpr std::int32_t file_read_service_tag = 73;
    static constexpr std::int32_t file_close_service_tag = 74;
    static constexpr std::int32_t file_directory_service_tag = 77;
    static constexpr std::size_t maximum_path_bytes = 1'024U;
    static constexpr std::size_t maximum_directory_entries = 65'536U;
    static constexpr std::size_t maximum_directory_depth = 8U;

    [[nodiscard]] static std::string normalize_remote_path(
        std::string path,
        bool allow_root = false);
    [[nodiscard]] static std::string rooted_backslash_path(const std::string& path);

    [[nodiscard]] static std::vector<std::uint8_t> encode_file_directory_request_pdu(
        const MmsFileDirectoryRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_file_directory_request_p_data(
        const MmsFileDirectoryRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsFileDirectoryResponse decode_file_directory_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::uint32_t expected_invoke_id,
        const std::string& directory_name = {});

    [[nodiscard]] static std::vector<std::uint8_t> encode_file_open_request_pdu(
        const MmsFileOpenRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_file_open_request_p_data(
        const MmsFileOpenRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsFileOpenResponse decode_file_open_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::uint32_t expected_invoke_id);

    [[nodiscard]] static std::vector<std::uint8_t> encode_file_read_request_pdu(
        const MmsFileReadRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_file_read_request_p_data(
        const MmsFileReadRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsFileReadResponse decode_file_read_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::uint32_t expected_invoke_id);

    [[nodiscard]] static std::vector<std::uint8_t> encode_file_close_request_pdu(
        const MmsFileCloseRequest& request);
    [[nodiscard]] static std::vector<std::uint8_t> encode_file_close_request_p_data(
        const MmsFileCloseRequest& request,
        std::uint32_t presentation_context_id = 3U);
    [[nodiscard]] static MmsFileCloseResponse decode_file_close_response(
        std::span<const std::uint8_t> presentation_or_mms_payload,
        std::uint32_t expected_invoke_id);
};

struct MmsFileDirectoryOptions final {
    std::size_t maximum_pages{64U};
    std::size_t maximum_entries{65'536U};
    std::size_t maximum_diagnostics{64U};
    std::size_t maximum_diagnostic_bytes{512U};
};

struct MmsFileDirectoryPage final {
    std::size_t page_number{};
    std::string continue_after;
    std::size_t response_entry_count{};
    bool more_follows{};
};

struct MmsFileDirectoryResult final {
    bool success{};
    std::string directory_name;
    std::vector<MmsFileDirectoryEntry> entries;
    std::vector<MmsFileDirectoryPage> pages;
    std::vector<MmsFileDiagnostic> diagnostics;
    MmsFileFailureKind failure_kind{MmsFileFailureKind::none};
    std::string message;
};

struct MmsFileTransferOptions final {
    std::uint64_t maximum_bytes{512ULL * 1024ULL * 1024ULL};
    std::size_t maximum_read_operations{100'000U};
    std::size_t maximum_block_bytes{1U * 1024U * 1024U};
    std::size_t maximum_diagnostics{64U};
    std::size_t maximum_diagnostic_bytes{512U};
    bool require_declared_size_match{};
};

struct MmsFileTransferProgress final {
    std::string remote_path;
    std::uint64_t bytes_transferred{};
    std::optional<std::uint64_t> expected_bytes;
    std::size_t read_operations{};
    bool complete{};
};

class MmsFileSink {
public:
    virtual ~MmsFileSink() = default;

    [[nodiscard]] virtual bool write(
        std::span<const std::uint8_t> data,
        std::string& error) noexcept = 0;
    [[nodiscard]] virtual bool flush(std::string& error) noexcept = 0;
    [[nodiscard]] virtual bool reset(std::string& error) noexcept = 0;
};

class MmsFileProgressSink {
public:
    virtual ~MmsFileProgressSink() = default;
    virtual void report(const MmsFileTransferProgress& progress) noexcept = 0;
};

struct MmsFileTransferResult final {
    bool success{};
    std::string remote_path;
    std::uint64_t bytes_transferred{};
    std::optional<std::uint64_t> expected_bytes;
    std::size_t read_operations{};
    bool remote_file_closed{};
    bool adaptive_fallback_attempted{};
    bool adaptive_fallback_succeeded{};
    MmsFileFailureKind failure_kind{MmsFileFailureKind::none};
    std::string message;
    std::vector<MmsFileDiagnostic> diagnostics;
    std::vector<MmsFileDiagnostic> primary_attempt_diagnostics;
    std::vector<MmsFileDiagnostic> fallback_attempt_diagnostics;
};

class MmsFileServiceChannel {
public:
    virtual ~MmsFileServiceChannel() = default;
    [[nodiscard]] virtual std::uint32_t next_invoke_id() = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> exchange_confirmed(
        std::span<const std::uint8_t> request,
        std::uint32_t expected_invoke_id,
        std::stop_token stop_token) = 0;
    [[nodiscard]] virtual bool associated() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t presentation_context_id() const noexcept = 0;
};

class MmsAssociationFileServiceChannel final : public MmsFileServiceChannel {
public:
    explicit MmsAssociationFileServiceChannel(MmsAssociationRuntime& association)
        : association_(association) {}

    [[nodiscard]] std::uint32_t next_invoke_id() override;
    [[nodiscard]] std::vector<std::uint8_t> exchange_confirmed(
        std::span<const std::uint8_t> request,
        std::uint32_t expected_invoke_id,
        std::stop_token stop_token) override;
    [[nodiscard]] bool associated() const noexcept override;
    [[nodiscard]] std::uint32_t presentation_context_id() const noexcept override;

private:
    MmsAssociationRuntime& association_;
};

class MmsFileTransferRuntime final {
public:
    explicit MmsFileTransferRuntime(MmsFileServiceChannel& channel)
        : channel_(channel) {}

    [[nodiscard]] MmsFileDirectoryResult list_directory(
        const std::string& directory_name = {},
        MmsFileDirectoryOptions options = {},
        std::stop_token stop_token = {});

    [[nodiscard]] MmsFileTransferResult download(
        const std::string& remote_path,
        MmsFileSink& sink,
        MmsFileTransferOptions options = {},
        MmsFileProgressSink* progress = nullptr,
        std::stop_token stop_token = {});

    [[nodiscard]] MmsFileTransferResult download_adaptive(
        const std::string& remote_path,
        MmsFileSink& sink,
        MmsFileTransferOptions options = {},
        MmsFileProgressSink* progress = nullptr,
        std::stop_token stop_token = {});

private:
    [[nodiscard]] MmsFileTransferResult download_attempt(
        const std::string& normalized_path,
        bool rooted_backslash,
        MmsFileSink& sink,
        const MmsFileTransferOptions& options,
        MmsFileProgressSink* progress,
        std::stop_token stop_token);

    MmsFileServiceChannel& channel_;
};

} // namespace ar::iec61850::mms
