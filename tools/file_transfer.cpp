// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/file_service.hpp"
#include "ariec61850/mms/tcp_transport.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace ar::iec61850;

enum class Command : std::uint8_t { list, download };

struct Options final {
    mms::MmsEndpoint endpoint;
    Command command{Command::list};
    std::string directory;
    std::string remote_path;
    std::string output_path;
    std::string json_output_path;
    std::size_t timeout_ms{30'000U};
    std::size_t maximum_pages{64U};
    std::size_t maximum_entries{65'536U};
    std::uint64_t maximum_bytes{512ULL * 1024ULL * 1024ULL};
    std::size_t maximum_reads{100'000U};
    std::size_t maximum_block_bytes{1U * 1024U * 1024U};
    bool json{};
};

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    for (const char character : value) {
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                output << '?';
            } else {
                output << character;
            }
            break;
        }
    }
    return output.str();
}

[[nodiscard]] std::string hex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (const auto value : bytes) {
        result.push_back(digits[(value >> 4U) & 0x0FU]);
        result.push_back(digits[value & 0x0FU]);
    }
    return result;
}

[[nodiscard]] std::size_t parse_size(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive bounded integer.");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::uint64_t parse_u64(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL) {
        throw std::invalid_argument(option + " requires a positive integer.");
    }
    return parsed;
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    const auto parsed = parse_size("port", value);
    if (parsed > 65'535U) {
        throw std::invalid_argument("port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

void usage() {
    std::cout
        << "Usage:\n"
        << "  ariec61850_file_transfer <host> [port] [list] [options]\n"
        << "  ariec61850_file_transfer <host> [port] download --remote PATH --output FILE [options]\n\n"
        << "List options:\n"
        << "  --directory PATH       Root is used by default.\n"
        << "  --max-pages N          Default 64.\n"
        << "  --max-entries N        Default 65536.\n\n"
        << "Download options:\n"
        << "  --remote PATH          Explicit remote file to read.\n"
        << "  --output FILE          Explicit local destination.\n"
        << "  --max-bytes N          Default 536870912.\n"
        << "  --max-reads N          Default 100000.\n"
        << "  --max-block-bytes N    Default 1048576.\n\n"
        << "Common options:\n"
        << "  --timeout-ms N         Connect/request timeout; default 30000.\n"
        << "  --json                 Print machine-readable evidence.\n"
        << "  --json-output FILE     Also write JSON evidence to a local file.\n"
        << "  --help                 Show this help.\n\n"
        << "The default command is read-only FileDirectory. Download must be explicit;\n"
        << "this tool never uploads, deletes, renames, or mutates remote files.\n";
}

[[nodiscard]] Options parse_options(const int argc, char** argv) {
    if (argc < 2) {
        throw std::invalid_argument("A host is required.");
    }
    Options options;
    options.endpoint.host = argv[1];
    options.endpoint.port = 102U;
    int index = 2;
    if (index < argc && std::string_view{argv[index]} != "list" &&
        std::string_view{argv[index]} != "download" &&
        std::string_view{argv[index]}.rfind("--", 0U) != 0U) {
        options.endpoint.port = parse_port(argv[index++]);
    }
    if (index < argc && std::string_view{argv[index]} == "list") {
        ++index;
    } else if (index < argc && std::string_view{argv[index]} == "download") {
        options.command = Command::download;
        ++index;
    }

    while (index < argc) {
        const std::string option = argv[index++];
        if (option == "--json") {
            options.json = true;
            continue;
        }
        if (option == "--help" || option == "-h") {
            usage();
            std::exit(0);
        }
        if (index >= argc) {
            throw std::invalid_argument(option + " requires a value.");
        }
        const std::string value = argv[index++];
        if (option == "--directory") options.directory = value;
        else if (option == "--remote") options.remote_path = value;
        else if (option == "--output") options.output_path = value;
        else if (option == "--json-output") options.json_output_path = value;
        else if (option == "--timeout-ms") options.timeout_ms = parse_size(option, value);
        else if (option == "--max-pages") options.maximum_pages = parse_size(option, value);
        else if (option == "--max-entries") options.maximum_entries = parse_size(option, value);
        else if (option == "--max-bytes") options.maximum_bytes = parse_u64(option, value);
        else if (option == "--max-reads") options.maximum_reads = parse_size(option, value);
        else if (option == "--max-block-bytes") {
            options.maximum_block_bytes = parse_size(option, value);
        } else {
            throw std::invalid_argument("Unknown option: " + option);
        }
    }
    if (options.command == Command::download &&
        (options.remote_path.empty() || options.output_path.empty())) {
        throw std::invalid_argument(
            "download requires both --remote PATH and --output FILE.");
    }
    return options;
}

class FileSink final : public mms::MmsFileSink {
public:
    explicit FileSink(std::string path) : path_(std::move(path)) {
        open();
        if (!stream_) {
            throw std::runtime_error("Unable to create local output file: " + path_);
        }
    }

    [[nodiscard]] bool write(
        const std::span<const std::uint8_t> data,
        std::string& error) noexcept override {
        stream_.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
        if (!stream_) {
            error = "write failed for local output file";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool flush(std::string& error) noexcept override {
        stream_.flush();
        if (!stream_) {
            error = "flush failed for local output file";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool reset(std::string& error) noexcept override {
        stream_.close();
        stream_.clear();
        open();
        if (!stream_) {
            error = "truncate/reset failed for local output file";
            return false;
        }
        return true;
    }

private:
    void open() {
        stream_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
    }

    std::string path_;
    std::ofstream stream_;
};

class ConsoleProgress final : public mms::MmsFileProgressSink {
public:
    void report(const mms::MmsFileTransferProgress& progress) noexcept override {
        std::cerr << "Transferred " << progress.bytes_transferred;
        if (progress.expected_bytes) {
            std::cerr << '/' << *progress.expected_bytes;
        }
        std::cerr << " bytes in " << progress.read_operations << " read(s)"
                  << (progress.complete ? " [complete]" : "") << ".\n";
    }
};

[[nodiscard]] std::string directory_json(
    const mms::MmsFileDirectoryResult& result) {
    std::ostringstream output;
    output << "{\"schemaVersion\":\"ariec61850-mms-file-directory-v1\","
           << "\"readOnly\":true,\"success\":"
           << (result.success ? "true" : "false") << ','
           << "\"directory\":\"" << json_escape(result.directory_name) << "\"," 
           << "\"message\":\"" << json_escape(result.message) << "\"," 
           << "\"pageCount\":" << result.pages.size() << ','
           << "\"entryCount\":" << result.entries.size() << ",\"pages\":[";
    for (std::size_t index = 0U; index < result.pages.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& page = result.pages[index];
        output << "{\"page\":" << page.page_number
               << ",\"continueAfter\":\"" << json_escape(page.continue_after)
               << "\",\"entryCount\":" << page.response_entry_count
               << ",\"moreFollows\":" << (page.more_follows ? "true" : "false")
               << '}';
    }
    output << "],\"entries\":[";
    for (std::size_t index = 0U; index < result.entries.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& entry = result.entries[index];
        output << "{\"name\":\"" << json_escape(entry.name)
               << "\",\"path\":\"" << json_escape(entry.path) << "\",";
        if (entry.size_bytes) output << "\"sizeBytes\":" << *entry.size_bytes << ',';
        else output << "\"sizeBytes\":null,";
        output << "\"lastModifiedHex\":\"" << hex(entry.last_modified)
               << "\",\"likelyDirectory\":"
               << (entry.likely_directory() ? "true" : "false") << '}';
    }
    output << "]}";
    return output.str();
}

[[nodiscard]] std::string transfer_json(
    const mms::MmsFileTransferResult& result) {
    std::ostringstream output;
    output << "{\"schemaVersion\":\"ariec61850-mms-file-transfer-v1\"," 
           << "\"readOnly\":true,\"success\":"
           << (result.success ? "true" : "false") << ','
           << "\"remotePath\":\"" << json_escape(result.remote_path) << "\"," 
           << "\"message\":\"" << json_escape(result.message) << "\"," 
           << "\"bytesTransferred\":" << result.bytes_transferred << ',';
    if (result.expected_bytes) output << "\"expectedBytes\":" << *result.expected_bytes << ',';
    else output << "\"expectedBytes\":null,";
    output << "\"readOperations\":" << result.read_operations << ','
           << "\"remoteFileClosed\":"
           << (result.remote_file_closed ? "true" : "false") << ','
           << "\"adaptiveFallbackAttempted\":"
           << (result.adaptive_fallback_attempted ? "true" : "false") << ','
           << "\"adaptiveFallbackSucceeded\":"
           << (result.adaptive_fallback_succeeded ? "true" : "false")
           << ",\"diagnostics\":[";
    for (std::size_t index = 0U; index < result.diagnostics.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& diagnostic = result.diagnostics[index];
        output << "{\"stage\":\"" << json_escape(diagnostic.stage)
               << "\",\"success\":" << (diagnostic.success ? "true" : "false")
               << ",\"message\":\"" << json_escape(diagnostic.message) << "\"}";
    }
    output << "]}";
    return output.str();
}

void emit_json(const std::string& json, const Options& options) {
    if (options.json) {
        std::cout << json << '\n';
    }
    if (!options.json_output_path.empty()) {
        std::ofstream output{
            options.json_output_path, std::ios::binary | std::ios::out | std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "Unable to create JSON evidence file: " + options.json_output_path);
        }
        output << json << '\n';
    }
}

[[nodiscard]] int exit_code(const mms::MmsFileFailureKind kind) noexcept {
    if (kind == mms::MmsFileFailureKind::sink) return 4;
    if (kind == mms::MmsFileFailureKind::cleanup) return 5;
    return 3;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 &&
        (std::string_view{argv[1]} == "--help" ||
         std::string_view{argv[1]} == "-h")) {
        usage();
        return 0;
    }
    try {
        const auto options = parse_options(argc, argv);
        if (options.timeout_ms > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument("--timeout-ms is too large.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = std::chrono::milliseconds{
            static_cast<std::int64_t>(options.timeout_ms)};
        association_options.request_timeout = association_options.connect_timeout;
        mms::TcpMmsByteTransport transport;
        mms::MmsAssociationRuntime association{transport, association_options};
        association.connect(options.endpoint);
        mms::MmsAssociationFileServiceChannel channel{association};
        mms::MmsFileTransferRuntime runtime{channel};

        int result_code = 0;
        if (options.command == Command::list) {
            mms::MmsFileDirectoryOptions directory_options;
            directory_options.maximum_pages = options.maximum_pages;
            directory_options.maximum_entries = options.maximum_entries;
            const auto result = runtime.list_directory(
                options.directory, directory_options);
            const auto json = directory_json(result);
            emit_json(json, options);
            if (!options.json) {
                std::cout << result.message << '\n';
                for (const auto& entry : result.entries) {
                    std::cout << (entry.likely_directory() ? "[DIR]  " : "[FILE] ")
                              << entry.path;
                    if (entry.size_bytes) std::cout << "  " << *entry.size_bytes << " bytes";
                    std::cout << '\n';
                }
            }
            if (!result.success) result_code = exit_code(result.failure_kind);
        } else {
            FileSink sink{options.output_path};
            ConsoleProgress progress;
            mms::MmsFileTransferOptions transfer_options;
            transfer_options.maximum_bytes = options.maximum_bytes;
            transfer_options.maximum_read_operations = options.maximum_reads;
            transfer_options.maximum_block_bytes = options.maximum_block_bytes;
            transfer_options.require_declared_size_match = true;
            const auto result = runtime.download_adaptive(
                options.remote_path, sink, transfer_options, &progress);
            const auto json = transfer_json(result);
            emit_json(json, options);
            if (!options.json) std::cout << result.message << '\n';
            if (!result.success) result_code = exit_code(result.failure_kind);
        }
        association.disconnect();
        return result_code;
    } catch (const std::invalid_argument& exception) {
        std::cerr << "Argument error: " << exception.what() << '\n';
        usage();
        return 2;
    } catch (const std::exception& exception) {
        std::cerr << "MMS file transfer tool failed: " << exception.what() << '\n';
        return 3;
    }
}
