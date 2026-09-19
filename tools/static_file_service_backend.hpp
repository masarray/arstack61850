// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/pdu.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ar::iec61850::host {

class StaticFileServiceRoot final {
public:
    StaticFileServiceRoot(std::filesystem::path root, const bool allow_delete)
        : allow_delete_{allow_delete} {
        std::error_code error;
        root_ = std::filesystem::weakly_canonical(std::move(root), error);
        if (error || root_.empty() || !std::filesystem::is_directory(root_, error) || error) {
            throw std::invalid_argument("MMS file root must resolve to an existing directory.");
        }
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] bool allow_delete() const noexcept { return allow_delete_; }

private:
    friend class StaticFileServiceSession;
    std::filesystem::path root_;
    bool allow_delete_{};
    std::mutex mutex_;
    std::unordered_map<std::string, std::size_t> open_counts_;
};

class StaticFileServiceSession final {
public:
    static constexpr std::size_t maximum_path_bytes = 512U;
    static constexpr std::size_t maximum_directory_entries = 4'096U;
    static constexpr std::size_t directory_page_entries = 64U;
    static constexpr std::size_t maximum_open_handles = 16U;
    static constexpr std::size_t read_block_bytes = 24U * 1024U;
    static constexpr std::uint64_t maximum_file_bytes = 512ULL * 1024ULL * 1024ULL;
    static constexpr std::size_t replay_capacity = 16U;
    static constexpr std::int32_t file_error_class = 11;
    static constexpr std::uint32_t file_error_other = 0U;
    static constexpr std::uint32_t file_error_busy = 2U;
    static constexpr std::uint32_t file_error_filename_syntax = 3U;
    static constexpr std::uint32_t file_error_position_invalid = 5U;
    static constexpr std::uint32_t file_error_access_denied = 6U;
    static constexpr std::uint32_t file_error_nonexistent = 7U;

    explicit StaticFileServiceSession(StaticFileServiceRoot& root) noexcept : root_{root} {}

    StaticFileServiceSession(const StaticFileServiceSession&) = delete;
    StaticFileServiceSession& operator=(const StaticFileServiceSession&) = delete;

    ~StaticFileServiceSession() { close_all(); }

    [[nodiscard]] static mms::MmsStaticConfirmedServiceExtensionResult callback(
        void* context,
        const mms::MmsWireConfirmedService service,
        const std::uint32_t invoke_id,
        const bool service_constructed,
        const std::span<const std::uint8_t> service_value,
        const std::span<std::uint8_t> response) noexcept {
        if (context == nullptr) return {};
        return static_cast<StaticFileServiceSession*>(context)->dispatch(
            service, invoke_id, service_constructed, service_value, response);
    }

    [[nodiscard]] mms::MmsStaticConfirmedServiceExtensionResult dispatch(
        const mms::MmsWireConfirmedService service,
        const std::uint32_t invoke_id,
        const bool service_constructed,
        const std::span<const std::uint8_t> service_value,
        const std::span<std::uint8_t> response) noexcept {
        if (!is_file_service(service)) return {};
        try {
            if (const auto* replay = find_replay(
                    service, invoke_id, service_constructed, service_value)) {
                return copy_response(replay->response, response);
            }

            std::vector<std::uint8_t> encoded;
            switch (service) {
            case mms::MmsWireConfirmedService::file_directory:
                encoded = directory(invoke_id, service_constructed, service_value);
                break;
            case mms::MmsWireConfirmedService::file_open:
                encoded = open(invoke_id, service_constructed, service_value);
                break;
            case mms::MmsWireConfirmedService::file_read:
                encoded = read(invoke_id, service_constructed, service_value);
                break;
            case mms::MmsWireConfirmedService::file_close:
                encoded = close(invoke_id, service_constructed, service_value);
                break;
            case mms::MmsWireConfirmedService::file_delete:
                encoded = remove(invoke_id, service_constructed, service_value);
                break;
            default:
                return {};
            }
            remember(service, invoke_id, service_constructed, service_value, encoded);
            return copy_response(encoded, response);
        } catch (const FileFault& fault) {
            return remember_error_and_copy(
                service, invoke_id, service_constructed, service_value,
                fault.code, response);
        } catch (...) {
            return remember_error_and_copy(
                service, invoke_id, service_constructed, service_value,
                file_error_other, response);
        }
    }

private:
    struct FileFault final : std::runtime_error {
        FileFault(const std::uint32_t value, std::string message)
            : std::runtime_error(std::move(message)), code{value} {}
        std::uint32_t code{};
    };

    struct Handle final {
        std::int32_t id{};
        std::filesystem::path canonical_path;
        std::ifstream stream;
        std::uint64_t size{};
        std::uint64_t offset{};
    };

    struct Replay final {
        mms::MmsWireConfirmedService service{mms::MmsWireConfirmedService::unknown};
        std::uint32_t invoke_id{};
        bool constructed{};
        std::vector<std::uint8_t> request_value;
        std::vector<std::uint8_t> response;
    };

    [[nodiscard]] static bool is_file_service(const mms::MmsWireConfirmedService service) noexcept {
        return service == mms::MmsWireConfirmedService::file_directory ||
            service == mms::MmsWireConfirmedService::file_open ||
            service == mms::MmsWireConfirmedService::file_read ||
            service == mms::MmsWireConfirmedService::file_close ||
            service == mms::MmsWireConfirmedService::file_delete;
    }

    [[nodiscard]] static mms::MmsStaticConfirmedServiceExtensionResult copy_response(
        const std::span<const std::uint8_t> encoded,
        const std::span<std::uint8_t> destination) noexcept {
        if (destination.size() < encoded.size()) {
            return {true, {wire::EncodeStatus::buffer_too_small, 0U, encoded.size()}};
        }
        std::copy(encoded.begin(), encoded.end(), destination.begin());
        return {true, {wire::EncodeStatus::ok, encoded.size(), encoded.size()}};
    }

    [[nodiscard]] mms::MmsStaticConfirmedServiceExtensionResult remember_error_and_copy(
        const mms::MmsWireConfirmedService service,
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> request_value,
        const std::uint32_t error_value,
        const std::span<std::uint8_t> response) noexcept {
        try {
            auto encoded = mms::MmsPduCodec::encode_confirmed_error(
                {invoke_id, file_error_class, error_value});
            remember(service, invoke_id, constructed, request_value, encoded);
            return copy_response(encoded, response);
        } catch (...) {
            return {true, {wire::EncodeStatus::value_out_of_range, 0U, 0U}};
        }
    }

    [[nodiscard]] const Replay* find_replay(
        const mms::MmsWireConfirmedService service,
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> request_value) const noexcept {
        const auto found = std::find_if(replays_.rbegin(), replays_.rend(),
            [&](const Replay& replay) {
                return replay.service == service && replay.invoke_id == invoke_id &&
                    replay.constructed == constructed && replay.request_value.size() == request_value.size() &&
                    std::equal(replay.request_value.begin(), replay.request_value.end(), request_value.begin());
            });
        return found == replays_.rend() ? nullptr : &*found;
    }

    void remember(
        const mms::MmsWireConfirmedService service,
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> request_value,
        const std::span<const std::uint8_t> response) {
        if (replays_.size() >= replay_capacity) replays_.pop_front();
        Replay replay;
        replay.service = service;
        replay.invoke_id = invoke_id;
        replay.constructed = constructed;
        replay.request_value.assign(request_value.begin(), request_value.end());
        replay.response.assign(response.begin(), response.end());
        replays_.push_back(std::move(replay));
    }

    [[nodiscard]] static std::string decode_graphic_name(
        const std::span<const std::uint8_t> bytes,
        const bool allow_empty) {
        std::vector<std::string> parts;
        collect_graphic(bytes, parts, 0U);
        if (parts.empty()) {
            if (allow_empty) return {};
            throw FileFault{file_error_filename_syntax, "FileName is missing."};
        }
        std::string joined;
        for (auto& part : parts) {
            if (!joined.empty() && !part.empty()) joined.push_back('/');
            joined += part;
        }
        return normalize_remote(joined, allow_empty);
    }

    static void collect_graphic(
        const std::span<const std::uint8_t> bytes,
        std::vector<std::string>& parts,
        const std::size_t depth) {
        if (depth > 8U) throw FileFault{file_error_filename_syntax, "FileName nesting is too deep."};
        for (const auto& child : asn1::BerReader::read_children(bytes)) {
            if (child.encoded_tag == 0x19U || child.encoded_tag == 0x1AU || child.encoded_tag == 0x16U) {
                parts.push_back(asn1::BerReader::read_ascii_string(child));
            } else if (child.constructed) {
                collect_graphic(child.value, parts, depth + 1U);
            }
        }
    }

    [[nodiscard]] static std::string normalize_remote(std::string value, const bool allow_empty) {
        if (value.size() > maximum_path_bytes) {
            throw FileFault{file_error_filename_syntax, "FileName exceeds the server bound."};
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        while (!value.empty() && value.front() == '/') value.erase(value.begin());
        while (!value.empty() && value.back() == '/') value.pop_back();
        if (value.empty()) {
            if (allow_empty) return {};
            throw FileFault{file_error_filename_syntax, "FileName is empty."};
        }
        std::string normalized;
        std::size_t begin{};
        while (begin <= value.size()) {
            const auto end = value.find('/', begin);
            const auto segment = value.substr(
                begin, end == std::string::npos ? std::string::npos : end - begin);
            if (segment.empty() || segment == "." || segment == ".." ||
                segment.find(':') != std::string::npos) {
                throw FileFault{file_error_filename_syntax, "Unsafe FileName segment."};
            }
            for (const char character : segment) {
                if (static_cast<unsigned char>(character) < 0x20U) {
                    throw FileFault{file_error_filename_syntax, "Control character in FileName."};
                }
            }
            if (!normalized.empty()) normalized.push_back('/');
            normalized += segment;
            if (end == std::string::npos) break;
            begin = end + 1U;
        }
        return normalized;
    }

    [[nodiscard]] static bool within_root(
        const std::filesystem::path& root,
        const std::filesystem::path& candidate) noexcept {
        auto root_it = root.begin();
        auto candidate_it = candidate.begin();
        for (; root_it != root.end(); ++root_it, ++candidate_it) {
            if (candidate_it == candidate.end() || *root_it != *candidate_it) return false;
        }
        return true;
    }

    [[nodiscard]] std::filesystem::path resolve(
        const std::string& remote,
        const bool allow_root) const {
        const auto normalized = normalize_remote(remote, allow_root);
        std::error_code error;
        const auto joined = normalized.empty()
            ? root_.root_
            : root_.root_ / std::filesystem::path{normalized};
        const auto canonical = std::filesystem::weakly_canonical(joined, error);
        if (error || !within_root(root_.root_, canonical)) {
            throw FileFault{file_error_access_denied, "FileName escapes the configured root."};
        }
        return canonical;
    }

    [[nodiscard]] static std::optional<std::int32_t> signed_integer_content(
        const std::span<const std::uint8_t> bytes) noexcept {
        if (bytes.empty() || bytes.size() > sizeof(std::int32_t)) return std::nullopt;
        std::int64_t value = (bytes.front() & 0x80U) != 0U ? -1LL : 0LL;
        for (const auto byte : bytes) value = (value << 8U) | static_cast<std::int64_t>(byte);
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
        return static_cast<std::int32_t>(value);
    }

    [[nodiscard]] static std::vector<std::uint8_t> context_tlv(
        const bool constructed,
        const std::int32_t tag,
        const std::span<const std::uint8_t> value) {
        return asn1::BerWriter::encode_tlv(asn1::BerClass::context_specific, constructed, tag, value);
    }

    [[nodiscard]] static std::vector<std::uint8_t> graphic(const std::string& value) {
        return asn1::BerWriter::encode_tlv(0x19U, asn1::BerWriter::encode_ascii(value));
    }

    [[nodiscard]] static std::vector<std::uint8_t> concat(
        const std::initializer_list<std::span<const std::uint8_t>> parts) {
        std::vector<std::uint8_t> result;
        std::size_t total{};
        for (const auto part : parts) total += part.size();
        result.reserve(total);
        for (const auto part : parts) result.insert(result.end(), part.begin(), part.end());
        return result;
    }

    [[nodiscard]] static std::vector<std::uint8_t> success(
        const std::uint32_t invoke_id,
        const mms::MmsWireConfirmedService service,
        const bool constructed,
        std::vector<std::uint8_t> value) {
        return mms::MmsPduCodec::encode_confirmed_response({
            invoke_id, static_cast<std::int32_t>(service), constructed, std::move(value)});
    }

    [[nodiscard]] std::vector<std::uint8_t> directory(
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> value) {
        if (!constructed) throw FileFault{file_error_filename_syntax, "FileDirectory must be constructed."};
        std::string directory_name;
        std::string continue_after;
        for (const auto& field : asn1::BerReader::read_children(value)) {
            if (field.tag_class != asn1::BerClass::context_specific || !field.constructed) continue;
            if (field.tag_number == 0) directory_name = decode_graphic_name(field.value, true);
            else if (field.tag_number == 1) continue_after = decode_graphic_name(field.value, true);
        }

        const auto path = resolve(directory_name, true);
        std::vector<std::pair<std::string, std::uint64_t>> entries;
        {
            std::scoped_lock lock{root_.mutex_};
            std::error_code error;
            if (!std::filesystem::is_directory(path, error) || error) {
                throw FileFault{file_error_nonexistent, "Directory does not exist."};
            }
            for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
                if (error) throw FileFault{file_error_access_denied, "Directory iteration failed."};
                if (entries.size() >= maximum_directory_entries) {
                    throw FileFault{file_error_other, "Directory exceeds the server entry bound."};
                }
                std::error_code type_error;
                if (entry.is_symlink(type_error) || type_error) continue;
                const auto name = entry.path().filename().generic_string();
                if (name.empty()) continue;
                if (entry.is_directory(type_error) && !type_error) {
                    entries.emplace_back(name, 0U);
                } else if (entry.is_regular_file(type_error) && !type_error) {
                    const auto size = entry.file_size(type_error);
                    if (!type_error && size <= maximum_file_bytes) entries.emplace_back(name, size);
                }
            }
        }
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        std::size_t start{};
        if (!continue_after.empty()) {
            start = static_cast<std::size_t>(std::distance(
                entries.begin(),
                std::upper_bound(entries.begin(), entries.end(), continue_after,
                    [](const std::string& key, const auto& entry) { return key < entry.first; })));
        }
        const auto end = std::min(entries.size(), start + directory_page_entries);
        std::vector<std::uint8_t> entry_bytes;
        for (std::size_t index = start; index < end; ++index) {
            const auto name = graphic(entries[index].first);
            const auto file_name = context_tlv(true, 0, name);
            const auto encoded_size = asn1::BerWriter::encode_unsigned_integer(entries[index].second);
            const auto size = context_tlv(false, 0, encoded_size);
            const auto attrs = context_tlv(true, 1, size);
            const auto item = asn1::BerWriter::encode_tlv(0x30U, concat({file_name, attrs}));
            entry_bytes.insert(entry_bytes.end(), item.begin(), item.end());
        }
        const auto sequence = asn1::BerWriter::encode_tlv(0x30U, entry_bytes);
        const auto list = context_tlv(true, 0, sequence);
        const auto more = context_tlv(false, 1, asn1::BerWriter::encode_boolean(end < entries.size()));
        return success(invoke_id, mms::MmsWireConfirmedService::file_directory, true, concat({list, more}));
    }

    [[nodiscard]] std::vector<std::uint8_t> open(
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> value) {
        if (!constructed) throw FileFault{file_error_filename_syntax, "FileOpen must be constructed."};
        std::string remote;
        std::uint64_t initial_position{};
        bool have_name{};
        for (const auto& field : asn1::BerReader::read_children(value)) {
            if (field.tag_class != asn1::BerClass::context_specific) continue;
            if (field.tag_number == 0 && field.constructed) {
                remote = decode_graphic_name(field.value, false);
                have_name = true;
            } else if (field.tag_number == 1 && !field.constructed) {
                const auto parsed = asn1::BerReader::read_unsigned_integer(field);
                if (!parsed) throw FileFault{file_error_position_invalid, "Invalid FileOpen initial position."};
                initial_position = *parsed;
            }
        }
        if (!have_name) throw FileFault{file_error_filename_syntax, "FileOpen has no FileName."};
        if (handles_.size() >= maximum_open_handles) throw FileFault{file_error_busy, "Too many open files."};
        const auto path = resolve(remote, false);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            throw FileFault{file_error_nonexistent, "File does not exist."};
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > maximum_file_bytes) throw FileFault{file_error_access_denied, "File size is unavailable or too large."};
        if (initial_position > size) throw FileFault{file_error_position_invalid, "Initial position is past end of file."};

        Handle handle;
        handle.id = allocate_frsm();
        handle.canonical_path = path;
        handle.size = size;
        handle.offset = initial_position;
        handle.stream.open(path, std::ios::binary);
        if (!handle.stream) throw FileFault{file_error_access_denied, "File cannot be opened."};
        handle.stream.seekg(static_cast<std::streamoff>(initial_position), std::ios::beg);
        if (!handle.stream) throw FileFault{file_error_position_invalid, "Initial seek failed."};
        {
            std::scoped_lock lock{root_.mutex_};
            ++root_.open_counts_[path.generic_string()];
        }
        const auto id = handle.id;
        handles_.emplace(id, std::move(handle));

        const auto frsm = context_tlv(false, 0, asn1::BerWriter::encode_signed_integer(id));
        const auto encoded_size = context_tlv(false, 0, asn1::BerWriter::encode_unsigned_integer(size));
        const auto attrs = context_tlv(true, 1, encoded_size);
        return success(invoke_id, mms::MmsWireConfirmedService::file_open, true, concat({frsm, attrs}));
    }

    [[nodiscard]] std::vector<std::uint8_t> read(
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> value) {
        if (constructed) throw FileFault{file_error_filename_syntax, "FileRead must be primitive."};
        const auto id = signed_integer_content(value);
        if (!id) throw FileFault{file_error_filename_syntax, "Invalid FRSM identifier."};
        const auto found = handles_.find(*id);
        if (found == handles_.end()) throw FileFault{file_error_nonexistent, "Unknown FRSM identifier."};
        auto& handle = found->second;
        const auto remaining = handle.size - std::min(handle.offset, handle.size);
        const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, read_block_bytes));
        std::vector<std::uint8_t> bytes(wanted);
        if (wanted != 0U) {
            handle.stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(wanted));
            const auto count = handle.stream.gcount();
            if (count < 0) throw FileFault{file_error_access_denied, "FileRead failed."};
            bytes.resize(static_cast<std::size_t>(count));
            handle.offset += static_cast<std::uint64_t>(count);
            if (static_cast<std::size_t>(count) != wanted && handle.offset < handle.size) {
                throw FileFault{file_error_access_denied, "FileRead ended before declared size."};
            }
        }
        const auto block = context_tlv(false, 0, bytes);
        const auto more = context_tlv(false, 1, asn1::BerWriter::encode_boolean(handle.offset < handle.size));
        return success(invoke_id, mms::MmsWireConfirmedService::file_read, true, concat({block, more}));
    }

    [[nodiscard]] std::vector<std::uint8_t> close(
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> value) {
        if (constructed) throw FileFault{file_error_filename_syntax, "FileClose must be primitive."};
        const auto id = signed_integer_content(value);
        if (!id) throw FileFault{file_error_filename_syntax, "Invalid FRSM identifier."};
        const auto found = handles_.find(*id);
        if (found == handles_.end()) throw FileFault{file_error_nonexistent, "Unknown FRSM identifier."};
        release_open_count(found->second.canonical_path);
        handles_.erase(found);
        return success(invoke_id, mms::MmsWireConfirmedService::file_close, false, {});
    }

    [[nodiscard]] std::vector<std::uint8_t> remove(
        const std::uint32_t invoke_id,
        const bool constructed,
        const std::span<const std::uint8_t> value) {
        if (!constructed) throw FileFault{file_error_filename_syntax, "FileDelete must be constructed."};
        if (!root_.allow_delete_) throw FileFault{file_error_access_denied, "FileDelete is disabled."};
        const auto remote = decode_graphic_name(value, false);
        const auto path = resolve(remote, false);
        std::scoped_lock lock{root_.mutex_};
        const auto opened = root_.open_counts_.find(path.generic_string());
        if (opened != root_.open_counts_.end() && opened->second != 0U) {
            throw FileFault{file_error_busy, "File is currently open."};
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            throw FileFault{file_error_nonexistent, "File does not exist."};
        }
        if (!std::filesystem::remove(path, error) || error) {
            throw FileFault{file_error_access_denied, "FileDelete failed."};
        }
        return success(invoke_id, mms::MmsWireConfirmedService::file_delete, false, {});
    }

    [[nodiscard]] std::int32_t allocate_frsm() {
        for (std::size_t attempt{}; attempt < maximum_open_handles + 1U; ++attempt) {
            if (next_frsm_ <= 0) next_frsm_ = 1;
            const auto candidate = next_frsm_++;
            if (handles_.find(candidate) == handles_.end()) return candidate;
        }
        throw FileFault{file_error_busy, "No FRSM identifier is available."};
    }

    void release_open_count(const std::filesystem::path& path) noexcept {
        try {
            std::scoped_lock lock{root_.mutex_};
            const auto found = root_.open_counts_.find(path.generic_string());
            if (found == root_.open_counts_.end()) return;
            if (found->second > 1U) --found->second;
            else root_.open_counts_.erase(found);
        } catch (...) {
        }
    }

    void close_all() noexcept {
        for (const auto& [id, handle] : handles_) {
            static_cast<void>(id);
            release_open_count(handle.canonical_path);
        }
        handles_.clear();
    }

    StaticFileServiceRoot& root_;
    std::map<std::int32_t, Handle> handles_;
    std::deque<Replay> replays_;
    std::int32_t next_frsm_{1};
};

} // namespace ar::iec61850::host
