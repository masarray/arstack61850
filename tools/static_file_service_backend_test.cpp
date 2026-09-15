// SPDX-License-Identifier: GPL-3.0-or-later

#include "static_file_service_backend.hpp"
#include "ariec61850/mms/file_service.hpp"
#include "ariec61850/mms/pdu.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace mms = ar::iec61850::mms;
namespace host = ar::iec61850::host;
namespace wire = ar::iec61850::wire;

namespace {

struct TemporaryDirectory final {
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("arstack-file-service-" + std::to_string(stamp));
        std::filesystem::create_directories(path / "nested");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

[[nodiscard]] bool expect(const bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

[[nodiscard]] std::vector<std::uint8_t> perform_exchange(
    host::StaticFileServiceSession& session,
    const std::vector<std::uint8_t>& request,
    const std::size_t capacity = 128U * 1024U) {
    const auto decoded = mms::MmsPduCodec::decode_confirmed_request(request);
    std::vector<std::uint8_t> response(capacity);
    const auto result = session.dispatch(
        static_cast<mms::MmsWireConfirmedService>(decoded.service_tag),
        decoded.invoke_id,
        decoded.service_constructed,
        decoded.service_value,
        response);
    if (!result.handled || !result.encoded.success()) return {};
    response.resize(result.encoded.bytes_written);
    return response;
}

[[nodiscard]] bool confirmed_file_error(
    const std::vector<std::uint8_t>& response,
    const std::uint32_t invoke_id,
    const std::uint32_t value) {
    if (response.empty()) return false;
    const auto envelope = mms::MmsPduCodec::decode_envelope(response);
    if (envelope.kind != mms::MmsPduKind::confirmed_error) return false;
    const auto error = mms::MmsPduCodec::decode_confirmed_error(envelope.mms_payload);
    return error.invoke_id == invoke_id &&
        error.error_class_tag == host::StaticFileServiceSession::file_error_class &&
        error.error_value == value;
}

} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        std::vector<std::uint8_t> source(60'000U);
        for (std::size_t index = 0U; index < source.size(); ++index) {
            source[index] = static_cast<std::uint8_t>((index * 17U + 3U) & 0xFFU);
        }
        {
            std::ofstream output{temporary.path / "FRA00028.dat", std::ios::binary};
            output.write(reinterpret_cast<const char*>(source.data()),
                         static_cast<std::streamsize>(source.size()));
        }
        {
            std::ofstream output{temporary.path / "FRA00028.cfg", std::ios::binary};
            output << "CFG\n";
        }

        host::StaticFileServiceRoot shared{temporary.path, true};
        host::StaticFileServiceSession session{shared};

        const auto directory_request = mms::MmsFileServiceCodec::encode_file_directory_request_pdu(
            {1U, {}, {}});
        const auto directory_wire = perform_exchange(session, directory_request);
        const auto directory = mms::MmsFileServiceCodec::decode_file_directory_response(
            directory_wire, 1U, {});
        if (!expect(directory.entries.size() == 3U, "root directory must expose three entries") ||
            !expect(directory.entries[0].name == "FRA00028.cfg", "directory order must be deterministic") ||
            !expect(directory.entries[1].name == "FRA00028.dat", "data file must be present") ||
            !expect(directory.entries[1].size_bytes == source.size(), "directory size must match file") ||
            !expect(!directory.more_follows, "small directory must fit one page")) return 1;

        const auto open_request = mms::MmsFileServiceCodec::encode_file_open_request_pdu(
            {2U, "FRA00028.dat", 0U, true});
        const auto open_wire = perform_exchange(session, open_request);
        const auto opened = mms::MmsFileServiceCodec::decode_file_open_response(open_wire, 2U);
        if (!expect(opened.file_size_bytes == source.size(), "FileOpen must report exact size") ||
            !expect(opened.frsm_id > 0, "FileOpen must allocate positive FRSM")) return 1;

        host::StaticFileServiceSession other_session{shared};
        const auto foreign_read_request = mms::MmsFileServiceCodec::encode_file_read_request_pdu(
            {3U, opened.frsm_id});
        const auto foreign_read = perform_exchange(other_session, foreign_read_request);
        if (!expect(confirmed_file_error(
                foreign_read, 3U, host::StaticFileServiceSession::file_error_nonexistent),
                "FRSM must be isolated per association")) return 1;

        std::vector<std::uint8_t> reconstructed;
        std::uint32_t invoke = 10U;
        bool more = true;
        while (more) {
            const auto read_request = mms::MmsFileServiceCodec::encode_file_read_request_pdu(
                {invoke, opened.frsm_id});
            const auto decoded_request = mms::MmsPduCodec::decode_confirmed_request(read_request);
            if (invoke == 10U) {
                std::array<std::uint8_t, 1U> tiny{};
                const auto first = session.dispatch(
                    mms::MmsWireConfirmedService::file_read,
                    decoded_request.invoke_id,
                    decoded_request.service_constructed,
                    decoded_request.service_value,
                    tiny);
                if (!expect(first.handled &&
                        first.encoded.status == wire::EncodeStatus::buffer_too_small,
                        "FileRead capacity retry must report required size")) return 1;
            }
            const auto read_wire = perform_exchange(session, read_request);
            const auto block = mms::MmsFileServiceCodec::decode_file_read_response(read_wire, invoke);
            reconstructed.insert(reconstructed.end(), block.data.begin(), block.data.end());
            more = block.more_follows;
            ++invoke;
        }
        if (!expect(reconstructed == source,
                "capacity retry must not advance FileRead cursor twice")) return 1;

        const auto close_request = mms::MmsFileServiceCodec::encode_file_close_request_pdu(
            {invoke++, opened.frsm_id});
        const auto close_wire = perform_exchange(session, close_request);
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_close_response(
            close_wire, invoke - 1U));

        host::StaticFileServiceRoot read_only{temporary.path, false};
        host::StaticFileServiceSession read_only_session{read_only};
        const auto denied_request = mms::MmsFileServiceCodec::encode_file_delete_request_pdu(
            {50U, "FRA00028.cfg", true});
        const auto denied = perform_exchange(read_only_session, denied_request);
        if (!expect(confirmed_file_error(
                denied, 50U, host::StaticFileServiceSession::file_error_access_denied),
                "FileDelete must require explicit server opt-in") ||
            !expect(std::filesystem::exists(temporary.path / "FRA00028.cfg"),
                "denied delete must not mutate storage")) return 1;

        const auto delete_request = mms::MmsFileServiceCodec::encode_file_delete_request_pdu(
            {51U, "FRA00028.cfg", true});
        const auto decoded_delete = mms::MmsPduCodec::decode_confirmed_request(delete_request);
        const auto capacity = session.dispatch(
            mms::MmsWireConfirmedService::file_delete,
            decoded_delete.invoke_id,
            decoded_delete.service_constructed,
            decoded_delete.service_value,
            {});
        if (!expect(capacity.handled &&
                capacity.encoded.status == wire::EncodeStatus::buffer_too_small,
                "FileDelete retry must cache successful destructive response") ||
            !expect(!std::filesystem::exists(temporary.path / "FRA00028.cfg"),
                "first FileDelete execution must remove the file")) return 1;
        const auto delete_wire = perform_exchange(session, delete_request);
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_delete_response(delete_wire, 51U));

        const auto refreshed_wire = perform_exchange(session,
            mms::MmsFileServiceCodec::encode_file_directory_request_pdu({52U, {}, {}}));
        const auto refreshed = mms::MmsFileServiceCodec::decode_file_directory_response(
            refreshed_wire, 52U, {});
        if (!expect(refreshed.entries.size() == 2U,
                "FileDirectory refresh must reflect successful delete")) return 1;

        std::cout << "HOST_FILE_SERVICE_PASS directory=pass open=pass read=pass close=pass "
                     "delete=pass retry_idempotent=pass frsm_isolation=pass sandbox=pass\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: exception: " << exception.what() << '\n';
        return 2;
    }
}
