// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/file_service.hpp"
#include "ariec61850/mms/pdu.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;
using ByteVector = std::vector<std::uint8_t>;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

template <typename Exception, typename Callable>
void check_throws(Callable&& callable) {
    try {
        std::invoke(std::forward<Callable>(callable));
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown.");
}

ByteVector concat(const std::initializer_list<std::span<const std::uint8_t>> parts) {
    ByteVector result;
    for (const auto part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

ByteVector confirmed_response(
    const std::uint32_t invoke_id,
    const std::int32_t service_tag,
    const bool constructed,
    const std::span<const std::uint8_t> value) {
    return mms::MmsPduCodec::wrap_p_data(
        mms::MmsPduCodec::encode_confirmed_response(
            {invoke_id, service_tag, constructed,
             ByteVector{value.begin(), value.end()}}));
}

ByteVector confirmed_error(
    const std::uint32_t invoke_id,
    const std::int32_t error_class,
    const std::uint32_t error_value) {
    return mms::MmsPduCodec::wrap_p_data(
        mms::MmsPduCodec::encode_confirmed_error(
            {invoke_id, error_class, error_value}));
}

ByteVector directory_entry(
    const std::string& name,
    const std::uint32_t size,
    const std::string& modified = "20260811010101Z") {
    using asn1::BerClass;
    using asn1::BerWriter;
    const auto graphic = BerWriter::encode_tlv(0x19U, BerWriter::encode_ascii(name));
    const auto file_name = BerWriter::encode_tlv(
        BerClass::context_specific, true, 0, graphic);
    const auto encoded_size = BerWriter::encode_tlv(
        BerClass::context_specific, false, 0,
        BerWriter::encode_unsigned_integer(size));
    const auto encoded_modified = BerWriter::encode_tlv(
        BerClass::context_specific, false, 1,
        BerWriter::encode_ascii(modified));
    const auto attributes = BerWriter::encode_tlv(
        BerClass::context_specific, true, 1,
        concat({encoded_size, encoded_modified}));
    return BerWriter::encode_tlv(0x30U, concat({file_name, attributes}));
}

ByteVector directory_response(
    const std::uint32_t invoke_id,
    const std::vector<ByteVector>& entries,
    const bool more_follows) {
    using asn1::BerClass;
    using asn1::BerWriter;
    ByteVector encoded_entries;
    for (const auto& entry : entries) {
        encoded_entries.insert(encoded_entries.end(), entry.begin(), entry.end());
    }
    const auto sequence = BerWriter::encode_tlv(0x30U, encoded_entries);
    const auto list = BerWriter::encode_tlv(
        BerClass::context_specific, true, 0, sequence);
    const auto more = BerWriter::encode_tlv(
        BerClass::context_specific, false, 1,
        BerWriter::encode_boolean(more_follows));
    return confirmed_response(
        invoke_id, mms::MmsFileServiceCodec::file_directory_service_tag,
        true, concat({list, more}));
}

ByteVector open_response(
    const std::uint32_t invoke_id,
    const std::int32_t frsm,
    const std::optional<std::uint32_t> size = std::nullopt) {
    using asn1::BerClass;
    using asn1::BerWriter;
    const auto id = BerWriter::encode_tlv(
        BerClass::context_specific, false, 0,
        BerWriter::encode_signed_integer(frsm));
    ByteVector value = id;
    if (size) {
        const auto encoded_size = BerWriter::encode_tlv(
            BerClass::context_specific, false, 0,
            BerWriter::encode_unsigned_integer(*size));
        const auto modified = BerWriter::encode_tlv(
            BerClass::context_specific, false, 1,
            BerWriter::encode_ascii("20260811010101Z"));
        const auto attributes = BerWriter::encode_tlv(
            BerClass::context_specific, true, 1,
            concat({encoded_size, modified}));
        value.insert(value.end(), attributes.begin(), attributes.end());
    }
    return confirmed_response(
        invoke_id, mms::MmsFileServiceCodec::file_open_service_tag, true, value);
}

ByteVector read_response(
    const std::uint32_t invoke_id,
    const std::span<const std::uint8_t> data,
    const bool more_follows) {
    using asn1::BerClass;
    using asn1::BerWriter;
    const auto block = BerWriter::encode_tlv(
        BerClass::context_specific, false, 0, data);
    const auto more = BerWriter::encode_tlv(
        BerClass::context_specific, false, 1,
        BerWriter::encode_boolean(more_follows));
    return confirmed_response(
        invoke_id, mms::MmsFileServiceCodec::file_read_service_tag,
        true, concat({block, more}));
}

ByteVector close_response(const std::uint32_t invoke_id) {
    return confirmed_response(
        invoke_id, mms::MmsFileServiceCodec::file_close_service_tag, false, {});
}

class FakeChannel final : public mms::MmsFileServiceChannel {
public:
    using Step = std::function<ByteVector(
        std::span<const std::uint8_t>, std::uint32_t, std::stop_token)>;

    [[nodiscard]] std::uint32_t next_invoke_id() override { return next_++; }

    [[nodiscard]] ByteVector exchange_confirmed(
        const std::span<const std::uint8_t> request,
        const std::uint32_t expected_invoke_id,
        const std::stop_token stop_token) override {
        if (steps.empty()) {
            throw std::runtime_error("FakeChannel script exhausted.");
        }
        auto step = std::move(steps.front());
        steps.pop_front();
        sent.emplace_back(request.begin(), request.end());
        return step(request, expected_invoke_id, stop_token);
    }

    [[nodiscard]] bool associated() const noexcept override { return connected; }
    [[nodiscard]] std::uint32_t presentation_context_id() const noexcept override {
        return 3U;
    }

    void respond(ByteVector response) {
        steps.push_back([response = std::move(response)](
                            std::span<const std::uint8_t>, std::uint32_t,
                            std::stop_token) { return response; });
    }

    std::uint32_t next_{1U};
    bool connected{true};
    std::deque<Step> steps;
    std::vector<ByteVector> sent;
};

class MemorySink final : public mms::MmsFileSink {
public:
    [[nodiscard]] bool write(
        const std::span<const std::uint8_t> data,
        std::string& error) noexcept override {
        if (fail_write) {
            error = "scripted sink write failure";
            return false;
        }
        bytes.insert(bytes.end(), data.begin(), data.end());
        return true;
    }

    [[nodiscard]] bool flush(std::string& error) noexcept override {
        if (fail_flush) {
            error = "scripted sink flush failure";
            return false;
        }
        flushed = true;
        return true;
    }

    [[nodiscard]] bool reset(std::string& error) noexcept override {
        ++reset_count;
        if (fail_reset) {
            error = "scripted sink reset failure";
            return false;
        }
        bytes.clear();
        flushed = false;
        return true;
    }

    ByteVector bytes;
    bool fail_write{};
    bool fail_flush{};
    bool fail_reset{};
    bool flushed{};
    std::size_t reset_count{};
};

std::int32_t sent_service_tag(const ByteVector& request) {
    return mms::MmsPduCodec::decode_confirmed_request(
        mms::MmsPduCodec::extract_mms_payload(request)).service_tag;
}

void codec_encodes_file_directory_high_tag_and_continuation() {
    const auto request = mms::MmsFileServiceCodec::encode_file_directory_request_pdu(
        {7U, "COMTRADE", "FRA00019.cfg"});
    const auto decoded = mms::MmsPduCodec::decode_confirmed_request(request);
    CHECK(decoded.invoke_id == 7U);
    CHECK(decoded.service_tag == 77);
    CHECK(decoded.service_constructed);
    const auto fields = asn1::BerReader::read_children(decoded.service_value);
    CHECK(fields.size() == 2U);
    CHECK(fields[0].tag_number == 0);
    CHECK(fields[1].tag_number == 1);
    CHECK(asn1::BerReader::read_ascii_string(
        asn1::BerReader::read_children(fields[0].value).front()) == "COMTRADE");
    CHECK(asn1::BerReader::read_ascii_string(
        asn1::BerReader::read_children(fields[1].value).front()) == "FRA00019.cfg");

    const auto root = mms::MmsFileServiceCodec::encode_file_directory_request_pdu(
        {8U, "/", {}});
    CHECK(mms::MmsPduCodec::decode_confirmed_request(root).service_value.empty());
}

void codec_decodes_directory_attributes_and_preserves_order() {
    const auto response = directory_response(9U, {
        directory_entry("COMTRADE/FRA00019.cfg", 2'048U, "A"),
        directory_entry("FRA00019.dat", 119'000U, "B")}, true);
    const auto decoded = mms::MmsFileServiceCodec::decode_file_directory_response(
        response, 9U, "COMTRADE");
    CHECK(decoded.more_follows);
    CHECK(decoded.entries.size() == 2U);
    CHECK(decoded.entries[0].path == "COMTRADE/FRA00019.cfg");
    CHECK(decoded.entries[0].size_bytes == 2'048U);
    CHECK(decoded.entries[0].last_modified == ByteVector{'A'});
    CHECK(decoded.entries[1].path == "COMTRADE/FRA00019.dat");
    CHECK(decoded.entries[1].size_bytes == 119'000U);
}

void codec_normalizes_paths_and_round_trips_signed_frsm() {
    const auto open = mms::MmsFileServiceCodec::encode_file_open_request_pdu(
        {17U, "COMTRADE\\TRIP_001.cfg", 0U, false});
    const auto decoded_open = mms::MmsPduCodec::decode_confirmed_request(open);
    CHECK(decoded_open.service_tag == 72);
    const auto fields = asn1::BerReader::read_children(decoded_open.service_value);
    const auto segments = asn1::BerReader::read_children(fields.front().value);
    CHECK(segments.size() == 2U);
    CHECK(asn1::BerReader::read_ascii_string(segments[0]) == "COMTRADE");
    CHECK(asn1::BerReader::read_ascii_string(segments[1]) == "TRIP_001.cfg");
    CHECK(!mms::MmsFileServiceCodec::encode_file_open_request_pdu(
        {18U, "FRA00019", 0U, false}).empty());
    CHECK(mms::MmsFileServiceCodec::rooted_backslash_path(
        "COMTRADE/FRA00028.dat") == "\\COMTRADE\\FRA00028.dat");
    check_throws<std::invalid_argument>([] {
        static_cast<void>(mms::MmsFileServiceCodec::encode_file_open_request_pdu(
            {1U, "../secret.cfg", 0U, false}));
    });

    for (const auto frsm : {
             std::numeric_limits<std::int32_t>::min(), -17, -1, 23,
             std::numeric_limits<std::int32_t>::max()}) {
        const auto read = mms::MmsFileServiceCodec::encode_file_read_request_pdu(
            {19U, frsm});
        const auto decoded_read = mms::MmsPduCodec::decode_confirmed_request(read);
        CHECK(decoded_read.service_tag == 73);
        asn1::BerTlv integer;
        std::size_t offset = 0U;
        CHECK(asn1::BerReader::try_read_tlv(
            asn1::BerWriter::encode_tlv(
                asn1::BerClass::context_specific, false, 73,
                decoded_read.service_value), offset, integer));
        CHECK(asn1::BerReader::read_signed_integer(integer) == frsm);

        const auto close = mms::MmsFileServiceCodec::encode_file_close_request_pdu(
            {20U, frsm});
        const auto decoded_close = mms::MmsPduCodec::decode_confirmed_request(close);
        CHECK(decoded_close.service_tag == 74);
        CHECK(asn1::BerReader::read_signed_integer(asn1::BerTlv{
            0U, asn1::BerClass::context_specific, false, 74,
            decoded_close.service_value}) == frsm);
    }

    const auto opened = mms::MmsFileServiceCodec::decode_file_open_response(
        open_response(21U, std::numeric_limits<std::int32_t>::min(), 4'096U), 21U);
    CHECK(opened.frsm_id == std::numeric_limits<std::int32_t>::min());
    CHECK(opened.file_size_bytes == 4'096U);
}

void codec_rejects_invoke_mismatch_malformed_and_trailing_data() {
    const auto valid = read_response(5U, ByteVector{1U, 2U}, false);
    try {
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_read_response(valid, 6U));
        CHECK(false);
    } catch (const mms::MmsFileServiceError& error) {
        CHECK(error.diagnostic().failure_kind ==
              mms::MmsFileFailureKind::invoke_id_mismatch);
    }
    auto trailing = valid;
    trailing.push_back(0U);
    try {
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_read_response(trailing, 5U));
        CHECK(false);
    } catch (const mms::MmsFileServiceError& error) {
        CHECK(error.diagnostic().failure_kind ==
              mms::MmsFileFailureKind::malformed_response);
    }
    auto truncated = valid;
    truncated.pop_back();
    check_throws<mms::MmsFileServiceError>([&] {
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_read_response(
            truncated, 5U));
    });
    CHECK(mms::MmsFileServiceCodec::decode_file_close_response(
        close_response(8U), 8U).invoke_id == 8U);
}

void directory_runtime_paginates_deduplicates_and_detects_no_progress() {
    FakeChannel channel;
    channel.respond(directory_response(1U, {
        directory_entry("B", 1U), directory_entry("A", 2U)}, true));
    channel.respond(directory_response(2U, {
        directory_entry("A", 2U), directory_entry("C", 3U)}, false));
    mms::MmsFileTransferRuntime runtime{channel};
    const auto result = runtime.list_directory();
    CHECK(result.success);
    CHECK(result.pages.size() == 2U);
    CHECK(result.pages[1].continue_after == "A");
    CHECK(result.entries.size() == 3U);
    CHECK(result.entries[0].name == "B");
    CHECK(result.entries[1].name == "A");
    CHECK(result.entries[2].name == "C");

    FakeChannel repeated;
    repeated.respond(directory_response(1U, {directory_entry("A", 1U)}, true));
    repeated.respond(directory_response(2U, {directory_entry("A", 1U)}, true));
    mms::MmsFileTransferRuntime repeated_runtime{repeated};
    const auto failed = repeated_runtime.list_directory();
    CHECK(!failed.success);
    CHECK(failed.failure_kind == mms::MmsFileFailureKind::protocol);
}

void runtime_streams_multiple_blocks_and_closes_negative_frsm() {
    FakeChannel channel;
    channel.respond(open_response(1U, -17, 5U));
    channel.respond(read_response(2U, ByteVector{1U, 2U, 3U}, true));
    channel.respond(read_response(3U, ByteVector{4U, 5U}, false));
    channel.respond(close_response(4U));
    MemorySink sink;
    mms::MmsFileTransferRuntime runtime{channel};
    mms::MmsFileTransferOptions options;
    options.require_declared_size_match = true;
    const auto result = runtime.download("FRA00019", sink, options);
    CHECK(result.success);
    CHECK(result.bytes_transferred == 5U);
    CHECK(result.read_operations == 2U);
    CHECK(result.remote_file_closed);
    CHECK(sink.bytes == (ByteVector{1U, 2U, 3U, 4U, 5U}));
    CHECK(sink.flushed);
    CHECK(channel.sent.size() == 4U);
    CHECK(sent_service_tag(channel.sent[0]) == 72);
    CHECK(sent_service_tag(channel.sent[1]) == 73);
    CHECK(sent_service_tag(channel.sent[3]) == 74);
}

void runtime_treats_zero_fileopen_size_as_unavailable() {
    FakeChannel channel;
    channel.respond(open_response(1U, 7, 0U));
    channel.respond(read_response(2U, ByteVector{8U, 8U, 2U}, false));
    channel.respond(close_response(3U));
    MemorySink sink;
    mms::MmsFileTransferRuntime runtime{channel};
    mms::MmsFileTransferOptions options;
    options.require_declared_size_match = true;
    const auto result = runtime.download("relay.inf", sink, options);
    CHECK(result.success);
    CHECK(!result.expected_bytes);
    CHECK(result.bytes_transferred == 3U);
    CHECK(result.remote_file_closed);
}

void runtime_bounds_empty_blocks_sizes_operations_and_sink_failures() {
    {
        FakeChannel channel;
        channel.respond(open_response(1U, 9, 10U));
        channel.respond(read_response(2U, {}, true));
        channel.respond(close_response(3U));
        MemorySink sink;
        mms::MmsFileTransferRuntime runtime{channel};
        const auto result = runtime.download("empty.cfg", sink);
        CHECK(!result.success);
        CHECK(result.failure_kind == mms::MmsFileFailureKind::protocol);
        CHECK(result.remote_file_closed);
    }
    {
        FakeChannel channel;
        channel.respond(open_response(1U, 10, 2U));
        channel.respond(read_response(2U, ByteVector{1U, 2U, 3U}, false));
        channel.respond(close_response(3U));
        MemorySink sink;
        mms::MmsFileTransferRuntime runtime{channel};
        const auto result = runtime.download("oversize.dat", sink);
        CHECK(!result.success);
        CHECK(result.bytes_transferred == 0U);
        CHECK(result.remote_file_closed);
    }
    {
        FakeChannel channel;
        channel.respond(open_response(1U, 11));
        channel.respond(read_response(2U, ByteVector{1U}, true));
        channel.respond(close_response(3U));
        MemorySink sink;
        mms::MmsFileTransferRuntime runtime{channel};
        mms::MmsFileTransferOptions options;
        options.maximum_read_operations = 1U;
        const auto result = runtime.download("bounded.dat", sink, options);
        CHECK(!result.success);
        CHECK(result.failure_kind == mms::MmsFileFailureKind::limit_exceeded);
        CHECK(result.remote_file_closed);
    }
    {
        FakeChannel channel;
        channel.respond(open_response(1U, 12));
        channel.respond(read_response(2U, ByteVector{1U}, false));
        channel.respond(close_response(3U));
        MemorySink sink;
        sink.fail_write = true;
        mms::MmsFileTransferRuntime runtime{channel};
        const auto result = runtime.download("sink.dat", sink);
        CHECK(!result.success);
        CHECK(result.failure_kind == mms::MmsFileFailureKind::sink);
        CHECK(result.remote_file_closed);
    }
}

void runtime_preserves_primary_failure_when_close_also_fails() {
    FakeChannel channel;
    channel.respond(open_response(1U, 21));
    channel.respond(confirmed_error(2U, 11, 3U));
    channel.respond(confirmed_error(3U, 11, 4U));
    MemorySink sink;
    mms::MmsFileTransferRuntime runtime{channel};
    const auto result = runtime.download("failure.dat", sink);
    CHECK(!result.success);
    CHECK(result.failure_kind == mms::MmsFileFailureKind::confirmed_error);
    CHECK(result.message.find("value=3") != std::string::npos);
    CHECK(!result.remote_file_closed);
    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.stage == "FileClose" && !diagnostic.success;
        }));
}

void adaptive_fallback_is_precise_and_resets_sink() {
    FakeChannel channel;
    channel.respond(confirmed_error(1U, 11, 7U));
    channel.steps.push_back([](
        const std::span<const std::uint8_t> request,
        const std::uint32_t invoke_id,
        std::stop_token) {
        const auto decoded = mms::MmsPduCodec::decode_confirmed_request(
            mms::MmsPduCodec::extract_mms_payload(request));
        const auto fields = asn1::BerReader::read_children(decoded.service_value);
        const auto names = asn1::BerReader::read_children(fields.front().value);
        CHECK(names.size() == 1U);
        CHECK(asn1::BerReader::read_ascii_string(names.front()) ==
              "\\COMTRADE\\FRA00028.dat");
        return open_response(invoke_id, -1, 2U);
    });
    channel.respond(read_response(3U, ByteVector{4U, 2U}, false));
    channel.respond(close_response(4U));
    MemorySink sink;
    mms::MmsFileTransferRuntime runtime{channel};
    const auto result = runtime.download_adaptive(
        "COMTRADE/FRA00028.dat", sink);
    CHECK(result.success);
    CHECK(result.adaptive_fallback_attempted);
    CHECK(result.adaptive_fallback_succeeded);
    CHECK(sink.reset_count == 1U);
    CHECK(result.primary_attempt_diagnostics.size() == 1U);
    CHECK(!result.fallback_attempt_diagnostics.empty());

    FakeChannel denied;
    denied.respond(confirmed_error(1U, 11, 3U));
    MemorySink denied_sink;
    mms::MmsFileTransferRuntime denied_runtime{denied};
    const auto denied_result = denied_runtime.download_adaptive(
        "denied.dat", denied_sink);
    CHECK(!denied_result.adaptive_fallback_attempted);
    CHECK(denied_sink.reset_count == 0U);
    CHECK(denied.steps.empty());
}

void adaptive_fallback_is_forbidden_after_any_read() {
    FakeChannel channel;
    channel.respond(open_response(1U, 22));
    channel.respond(read_response(2U, ByteVector{1U}, true));
    channel.respond(confirmed_error(3U, 11, 7U));
    channel.respond(close_response(4U));
    MemorySink sink;
    mms::MmsFileTransferRuntime runtime{channel};
    const auto result = runtime.download_adaptive("partial.dat", sink);
    CHECK(!result.success);
    CHECK(result.bytes_transferred == 1U);
    CHECK(result.read_operations == 2U);
    CHECK(!result.adaptive_fallback_attempted);
    CHECK(sink.reset_count == 0U);
}

void cancellation_after_open_still_closes() {
    FakeChannel channel;
    std::stop_source source;
    channel.steps.push_back([&source](
        std::span<const std::uint8_t>, const std::uint32_t invoke_id,
        std::stop_token) {
        source.request_stop();
        return open_response(invoke_id, 31, 1U);
    });
    channel.respond(close_response(2U));
    MemorySink sink;
    mms::MmsFileTransferRuntime runtime{channel};
    const auto result = runtime.download(
        "cancel.dat", sink, {}, nullptr, source.get_token());
    CHECK(!result.success);
    CHECK(result.failure_kind == mms::MmsFileFailureKind::cancelled);
    CHECK(result.remote_file_closed);
    CHECK(channel.sent.size() == 2U);
    CHECK(sent_service_tag(channel.sent.back()) == 74);
}

void timeout_preserves_evidence_when_association_drops() {
    FakeChannel channel;
    channel.respond(open_response(1U, 41));
    channel.steps.push_back([&channel](
        std::span<const std::uint8_t>, std::uint32_t, std::stop_token) -> ByteVector {
        channel.connected = false;
        throw mms::MmsTransportTimeoutError("scripted timeout");
    });
    MemorySink sink;
    mms::MmsFileTransferRuntime runtime{channel};
    const auto result = runtime.download("timeout.dat", sink);
    CHECK(!result.success);
    CHECK(result.failure_kind == mms::MmsFileFailureKind::timed_out);
    CHECK(!result.remote_file_closed);
    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.failure_kind == mms::MmsFileFailureKind::timed_out;
        }));
}

void portable_core_has_no_host_file_or_socket_dependencies() {
    const std::vector<std::string> files{
        std::string{ARIEC61850_SOURCE_DIR} + "/include/ariec61850/mms/file_service.hpp",
        std::string{ARIEC61850_SOURCE_DIR} + "/src/mms/file_service.cpp"};
    const std::vector<std::string> forbidden{
        "<filesystem>", "<fstream>", "std::filesystem", "std::ifstream",
        "std::ofstream", "winsock", "sys/socket", "TcpMmsByteTransport",
        "std::thread"};
    for (const auto& path : files) {
        std::ifstream input{path, std::ios::binary};
        CHECK(input.good());
        const std::string source{
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        for (const auto& token : forbidden) {
            CHECK(source.find(token) == std::string::npos);
        }
    }
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"codec file-directory request", codec_encodes_file_directory_high_tag_and_continuation},
        {"codec file-directory response", codec_decodes_directory_attributes_and_preserves_order},
        {"codec path and signed FRSM", codec_normalizes_paths_and_round_trips_signed_frsm},
        {"codec malformed evidence", codec_rejects_invoke_mismatch_malformed_and_trailing_data},
        {"directory bounded pagination", directory_runtime_paginates_deduplicates_and_detects_no_progress},
        {"runtime streaming and close", runtime_streams_multiple_blocks_and_closes_negative_frsm},
        {"runtime zero declared size", runtime_treats_zero_fileopen_size_as_unavailable},
        {"runtime bounds and sink", runtime_bounds_empty_blocks_sizes_operations_and_sink_failures},
        {"runtime primary error preservation", runtime_preserves_primary_failure_when_close_also_fails},
        {"adaptive precise fallback", adaptive_fallback_is_precise_and_resets_sink},
        {"adaptive forbids retry after read", adaptive_fallback_is_forbidden_after_any_read},
        {"cancellation cleanup", cancellation_after_open_still_closes},
        {"timeout evidence", timeout_preserves_evidence_when_association_drops},
        {"portable dependency boundary", portable_core_has_no_host_file_or_socket_dependencies},
    };

    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
            return 1;
        }
    }
    std::cout << "MMS file-service tests passed: " << passed << '/' << tests.size() << ".\n";
    return 0;
}
