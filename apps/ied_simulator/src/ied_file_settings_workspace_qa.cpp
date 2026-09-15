// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsFileSettingsController.hpp"
#include "MmsSettingGroupPolicy.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/embedded/io.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/file_service.hpp"
#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/mms/static_server_session.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mms = ar::iec61850::mms;
namespace asn1 = ar::iec61850::asn1;
namespace embedded = ar::iec61850::embedded;
namespace wire = ar::iec61850::wire;

namespace {
using ByteVector = std::vector<std::uint8_t>;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

bool waitFor(const std::function<bool()>& predicate, const int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

mms::MmsTypeSpecification scalarType(
    const mms::MmsTypeKind kind,
    std::string name) {
    mms::MmsTypeSpecification type;
    type.kind = kind;
    type.name = std::move(name);
    return type;
}

[[nodiscard]] wire::EncodeResult copyEncoded(
    const std::vector<std::uint8_t>& encoded,
    const std::span<std::uint8_t> destination) noexcept {
    if (destination.size() < encoded.size()) {
        return {wire::EncodeStatus::buffer_too_small, 0U, encoded.size()};
    }
    std::copy(encoded.begin(), encoded.end(), destination.begin());
    return {wire::EncodeStatus::ok, encoded.size(), encoded.size()};
}

[[nodiscard]] wire::EncodeResult readUnsigned(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    try {
        const auto value = *static_cast<const std::uint64_t*>(context);
        return copyEncoded(
            mms::MmsDataCodec::encode(mms::MmsDataValue::unsigned_integer(value)),
            destination);
    } catch (...) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
}

[[nodiscard]] wire::EncodeResult readBool(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    try {
        const auto value = *static_cast<const bool*>(context);
        return copyEncoded(
            mms::MmsDataCodec::encode(mms::MmsDataValue::boolean(value)),
            destination);
    } catch (...) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
}

struct SettingFixtureState final {
    std::uint64_t active{1U};
    std::uint64_t count{4U};
    std::uint64_t edit{};
    bool confirm{};
    std::uint64_t lastActivationTime{1'700'000'000U};
    std::uint64_t writeCount{};
};

[[nodiscard]] mms::MmsStaticWriteResult writeActiveGroup(
    void* context,
    const std::span<const std::uint8_t> encodedData) noexcept {
    auto* state = static_cast<SettingFixtureState*>(context);
    if (state == nullptr || encodedData.empty()) return {false, 11U};
    try {
        const auto values = mms::MmsDataCodec::decode_all(encodedData);
        if (values.size() != 1U) return {false, 11U};
        std::uint64_t requested{};
        if (values.front().kind() == mms::MmsDataKind::unsigned_integer) {
            requested = std::get<std::uint64_t>(values.front().value());
        } else if (values.front().kind() == mms::MmsDataKind::integer) {
            const auto signedValue = std::get<std::int64_t>(values.front().value());
            if (signedValue < 0) return {false, 11U};
            requested = static_cast<std::uint64_t>(signedValue);
        } else {
            return {false, 11U};
        }
        if (requested == 0U || requested > state->count || state->edit != 0U || state->confirm) {
            return {false, 11U};
        }
        state->active = requested;
        ++state->lastActivationTime;
        ++state->writeCount;
        return {true, 0U};
    } catch (...) {
        return {false, 11U};
    }
}

[[nodiscard]] embedded::IoResult qtReceive(
    void* context,
    const std::span<std::uint8_t> destination) noexcept {
    auto* socket = static_cast<QTcpSocket*>(context);
    if (socket == nullptr || destination.empty()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }
    if (socket->bytesAvailable() <= 0) {
        return socket->state() == QAbstractSocket::UnconnectedState
            ? embedded::IoResult{embedded::IoStatus::closed, 0U}
            : embedded::IoResult{embedded::IoStatus::would_block, 0U};
    }
    const auto count = socket->read(
        reinterpret_cast<char*>(destination.data()),
        static_cast<qint64>(destination.size()));
    if (count > 0) return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    if (count == 0) return {embedded::IoStatus::would_block, 0U};
    return {embedded::IoStatus::io_error, 0U};
}

[[nodiscard]] embedded::IoResult qtSend(
    void* context,
    const std::span<const std::uint8_t> bytes) noexcept {
    auto* socket = static_cast<QTcpSocket*>(context);
    if (socket == nullptr || bytes.empty()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }
    if (socket->state() == QAbstractSocket::UnconnectedState) {
        return {embedded::IoStatus::closed, 0U};
    }
    const auto count = socket->write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<qint64>(bytes.size()));
    if (count > 0) {
        socket->flush();
        return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    }
    if (count == 0) return {embedded::IoStatus::would_block, 0U};
    return {embedded::IoStatus::io_error, 0U};
}

class DeterministicSettingGroupFixture final {
public:
    DeterministicSettingGroupFixture() {
        unsignedType_ = mms::MmsServiceCodec::encode_type_specification(
            scalarType(mms::MmsTypeKind::unsigned_integer, "INT32U"));
        boolType_ = mms::MmsServiceCodec::encode_type_specification(
            scalarType(mms::MmsTypeKind::boolean, "BOOLEAN"));

        objects_[0] = mms::MmsStaticObjectEntry{
            "SETLD0", "LLN0$SP$SGCB$ActSG", unsignedType_, readUnsigned, &state_.active,
            false, writeActiveGroup, &state_};
        objects_[1] = mms::MmsStaticObjectEntry{
            "SETLD0", "LLN0$SG$SGCB$NumOfSG", unsignedType_, readUnsigned, &state_.count};
        objects_[2] = mms::MmsStaticObjectEntry{
            "SETLD0", "LLN0$SE$SGCB$EditSG", unsignedType_, readUnsigned, &state_.edit};
        objects_[3] = mms::MmsStaticObjectEntry{
            "SETLD0", "LLN0$SP$SGCB$CnfEdit", boolType_, readBool, &state_.confirm};
        objects_[4] = mms::MmsStaticObjectEntry{
            "SETLD0", "LLN0$SP$SGCB$LActTm", unsignedType_, readUnsigned,
            &state_.lastActivationTime};

        table_ = std::make_unique<mms::MmsStaticObjectTable>(
            std::span<const mms::MmsStaticObjectEntry>{objects_});
        dispatcher_ = std::make_unique<mms::MmsStaticApplicationDispatcher>(*table_);
        QObject::connect(&server_, &QTcpServer::newConnection, [&] { acceptConnection(); });
        QObject::connect(&pollTimer_, &QTimer::timeout, [&] { poll(); });
        pollTimer_.setInterval(1);
    }

    bool start() {
        if (!table_->valid()) return false;
        if (!server_.listen(QHostAddress::LocalHost, 0)) return false;
        pollTimer_.start();
        return true;
    }

    [[nodiscard]] quint16 port() const noexcept { return server_.serverPort(); }
    [[nodiscard]] std::uint64_t active() const noexcept { return state_.active; }
    [[nodiscard]] std::uint64_t writeCount() const noexcept { return state_.writeCount; }
    [[nodiscard]] int acceptedConnections() const noexcept { return acceptedConnections_; }
    void setEdit(const std::uint64_t group) noexcept { state_.edit = group; }

private:
    void acceptConnection() {
        while (server_.hasPendingConnections()) {
            auto* incoming = server_.nextPendingConnection();
            if (socket_ != nullptr) {
                incoming->disconnectFromHost();
                incoming->deleteLater();
                continue;
            }
            ++acceptedConnections_;
            socket_ = incoming;
            mms::MmsStaticConnectionPolicy policy;
            policy.association_id = 1U;
            policy.owner[0] = 0x51U;
            policy.owner_size = 1U;
            runtime_ = std::make_unique<mms::MmsStaticConnectionRuntime>(*dispatcher_, policy);
            const embedded::TcpByteStream stream{socket_, qtSend, qtReceive};
            session_ = std::make_unique<mms::MmsStaticServerSession>(
                *runtime_, stream,
                mms::MmsStaticServerSessionBuffers{receive_, response_, workspace_});
            QObject::connect(socket_, &QTcpSocket::disconnected, [&] {
                if (runtime_) runtime_->close_transport();
            });
        }
    }

    void poll() {
        if (!session_) return;
        for (int iteration = 0; iteration < 32; ++iteration) {
            const auto result = session_->poll_once();
            if (result.terminal()) {
                if (runtime_) runtime_->close_transport();
                if (socket_) socket_->deleteLater();
                socket_ = nullptr;
                session_.reset();
                runtime_.reset();
                return;
            }
            if ((result.status == mms::MmsStaticServerSessionStatus::would_block ||
                 result.status == mms::MmsStaticServerSessionStatus::timed_out) &&
                (socket_ == nullptr || socket_->bytesAvailable() == 0)) {
                break;
            }
        }
    }

    SettingFixtureState state_;
    std::vector<std::uint8_t> unsignedType_;
    std::vector<std::uint8_t> boolType_;
    std::array<mms::MmsStaticObjectEntry, 5U> objects_{};
    std::unique_ptr<mms::MmsStaticObjectTable> table_;
    std::unique_ptr<mms::MmsStaticApplicationDispatcher> dispatcher_;
    QTcpServer server_;
    QTimer pollTimer_;
    QTcpSocket* socket_{};
    int acceptedConnections_{};
    std::unique_ptr<mms::MmsStaticConnectionRuntime> runtime_;
    std::unique_ptr<mms::MmsStaticServerSession> session_;
    std::array<std::uint8_t, 32'768U> receive_{};
    std::array<std::uint8_t, 32'768U> response_{};
    std::array<std::uint8_t, 8'192U> workspace_{};
};

ByteVector concat(const std::initializer_list<std::span<const std::uint8_t>> parts) {
    ByteVector result;
    for (const auto part : parts) result.insert(result.end(), part.begin(), part.end());
    return result;
}

ByteVector confirmedResponse(
    const std::uint32_t invokeId,
    const std::int32_t serviceTag,
    const bool constructed,
    const std::span<const std::uint8_t> value) {
    return mms::MmsPduCodec::wrap_p_data(
        mms::MmsPduCodec::encode_confirmed_response(
            {invokeId, serviceTag, constructed, ByteVector{value.begin(), value.end()}}));
}

ByteVector directoryEntry(const std::string& name, const std::uint32_t size) {
    const auto graphic = asn1::BerWriter::encode_tlv(
        0x19U, asn1::BerWriter::encode_ascii(name));
    const auto fileName = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, true, 0, graphic);
    const auto encodedSize = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 0,
        asn1::BerWriter::encode_unsigned_integer(size));
    const auto modified = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 1,
        asn1::BerWriter::encode_ascii("20260914070000Z"));
    const auto attributes = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, true, 1,
        concat({encodedSize, modified}));
    return asn1::BerWriter::encode_tlv(0x30U, concat({fileName, attributes}));
}

ByteVector directoryResponse(
    const std::uint32_t invokeId,
    const std::vector<ByteVector>& entries,
    const bool moreFollows) {
    ByteVector encodedEntries;
    for (const auto& entry : entries) {
        encodedEntries.insert(encodedEntries.end(), entry.begin(), entry.end());
    }
    const auto sequence = asn1::BerWriter::encode_tlv(0x30U, encodedEntries);
    const auto list = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, true, 0, sequence);
    const auto more = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 1,
        asn1::BerWriter::encode_boolean(moreFollows));
    return confirmedResponse(
        invokeId, mms::MmsFileServiceCodec::file_directory_service_tag,
        true, concat({list, more}));
}

ByteVector openResponse(
    const std::uint32_t invokeId,
    const std::int32_t frsm,
    const std::uint32_t size) {
    const auto id = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 0,
        asn1::BerWriter::encode_signed_integer(frsm));
    const auto encodedSize = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 0,
        asn1::BerWriter::encode_unsigned_integer(size));
    const auto modified = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 1,
        asn1::BerWriter::encode_ascii("20260914070000Z"));
    const auto attributes = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, true, 1,
        concat({encodedSize, modified}));
    return confirmedResponse(
        invokeId, mms::MmsFileServiceCodec::file_open_service_tag,
        true, concat({id, attributes}));
}

ByteVector readResponse(
    const std::uint32_t invokeId,
    const std::span<const std::uint8_t> data,
    const bool moreFollows) {
    const auto block = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 0, data);
    const auto more = asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, false, 1,
        asn1::BerWriter::encode_boolean(moreFollows));
    return confirmedResponse(
        invokeId, mms::MmsFileServiceCodec::file_read_service_tag,
        true, concat({block, more}));
}

ByteVector closeResponse(const std::uint32_t invokeId) {
    return confirmedResponse(
        invokeId, mms::MmsFileServiceCodec::file_close_service_tag, false, {});
}

class FakeFileChannel final : public mms::MmsFileServiceChannel {
public:
    using Step = std::function<ByteVector(
        std::span<const std::uint8_t>, std::uint32_t, std::stop_token)>;

    [[nodiscard]] std::uint32_t next_invoke_id() override { return next_++; }
    [[nodiscard]] ByteVector exchange_confirmed(
        const std::span<const std::uint8_t> request,
        const std::uint32_t invokeId,
        const std::stop_token stopToken) override {
        if (steps.empty()) throw std::runtime_error("Fake file channel script exhausted.");
        auto step = std::move(steps.front());
        steps.pop_front();
        sent.emplace_back(request.begin(), request.end());
        return step(request, invokeId, stopToken);
    }
    [[nodiscard]] bool associated() const noexcept override { return true; }
    [[nodiscard]] std::uint32_t presentation_context_id() const noexcept override { return 3U; }

    void respond(ByteVector response) {
        steps.push_back([response = std::move(response)](
            std::span<const std::uint8_t>, std::uint32_t, std::stop_token) {
            return response;
        });
    }

    std::uint32_t next_{1U};
    std::deque<Step> steps;
    std::vector<ByteVector> sent;
};

class MemorySink final : public mms::MmsFileSink {
public:
    [[nodiscard]] bool write(
        const std::span<const std::uint8_t> data,
        std::string& error) noexcept override {
        if (failWrite) {
            error = "scripted sink write failure";
            return false;
        }
        bytes.insert(bytes.end(), data.begin(), data.end());
        return true;
    }
    [[nodiscard]] bool flush(std::string&) noexcept override {
        flushed = true;
        return true;
    }
    [[nodiscard]] bool reset(std::string&) noexcept override {
        bytes.clear();
        flushed = false;
        return true;
    }

    ByteVector bytes;
    bool failWrite{};
    bool flushed{};
};

void proveFileRuntime() {
    FakeFileChannel listing;
    listing.respond(directoryResponse(1U, {
        directoryEntry("TRIP_A.cfg", 3U), directoryEntry("TRIP_A.dat", 6U)}, true));
    listing.respond(directoryResponse(2U, {
        directoryEntry("IED.CID", 9U)}, false));
    mms::MmsFileTransferRuntime directoryRuntime{listing};
    mms::MmsFileDirectoryOptions directoryOptions;
    directoryOptions.maximum_pages = 4U;
    directoryOptions.maximum_entries = 8U;
    const auto directory = directoryRuntime.list_directory("COMTRADE", directoryOptions);
    CHECK(directory.success);
    CHECK(directory.pages.size() == 2U);
    CHECK(directory.entries.size() == 3U);

    FakeFileChannel download;
    download.respond(openResponse(1U, 7, 6U));
    download.respond(readResponse(2U, ByteVector{'A', 'B', 'C'}, true));
    download.respond(readResponse(3U, ByteVector{'D', 'E', 'F'}, false));
    download.respond(closeResponse(4U));
    MemorySink sink;
    mms::MmsFileTransferRuntime downloadRuntime{download};
    mms::MmsFileTransferOptions transferOptions;
    transferOptions.maximum_bytes = 64U;
    transferOptions.maximum_read_operations = 8U;
    transferOptions.maximum_block_bytes = 16U;
    const auto transfer = downloadRuntime.download(
        "COMTRADE/TRIP_A.cfg", sink, transferOptions);
    CHECK(transfer.success);
    CHECK(transfer.remote_file_closed);
    CHECK(transfer.bytes_transferred == 6U);
    CHECK(transfer.read_operations == 2U);
    CHECK(sink.bytes == ByteVector({'A', 'B', 'C', 'D', 'E', 'F'}));
    CHECK(sink.flushed);

    FakeFileChannel cleanup;
    cleanup.respond(openResponse(1U, 9, 3U));
    cleanup.respond(readResponse(2U, ByteVector{'B', 'A', 'D'}, false));
    cleanup.respond(closeResponse(3U));
    MemorySink failingSink;
    failingSink.failWrite = true;
    mms::MmsFileTransferRuntime cleanupRuntime{cleanup};
    const auto failed = cleanupRuntime.download(
        "COMTRADE/FAIL.dat", failingSink, transferOptions);
    CHECK(!failed.success);
    CHECK(failed.remote_file_closed);
    CHECK(failed.failure_kind == mms::MmsFileFailureKind::sink);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    qputenv("ARSTACK_IEDSIM_QA", "1");

    try {
        MmsFileSettingsController disconnected;
        CHECK(!disconnected.browseDirectory(QString{}));
        CHECK(!disconnected.downloadFile(QStringLiteral("A"), QStringLiteral("B")));
        CHECK(!disconnected.activateSelectedSettingGroup(1));

        proveFileRuntime();

        DeterministicSettingGroupFixture fixture;
        CHECK(fixture.start());
        CHECK(fixture.port() != 0U);

        MmsFileSettingsController controller;
        controller.setHost(QStringLiteral("127.0.0.1"));
        controller.setPort(fixture.port());
        CHECK(controller.connectToIed());
        CHECK(waitFor([&] { return controller.connected(); }, 10'000));
        CHECK(controller.lastError().isEmpty());
        CHECK(controller.settingGroupCount() == 1);
        CHECK(controller.selectedSettingGroupIndex() == 0);

        const auto initial = controller.selectedSettingGroup();
        CHECK(initial.value(QStringLiteral("attributeCount")).toInt() == 5);
        CHECK(initial.value(QStringLiteral("numOfSG")).toULongLong() == 4U);
        CHECK(initial.value(QStringLiteral("actSG")).toULongLong() == 1U);
        CHECK(initial.value(QStringLiteral("editSG")).toULongLong() == 0U);
        CHECK(!initial.value(QStringLiteral("cnfEdit")).toBool());
        CHECK(initial.value(QStringLiteral("canActivate")).toBool());
        CHECK(!initial.value(QStringLiteral("fullEditSupported")).toBool());
        CHECK(fixture.acceptedConnections() == 1);

        CHECK(controller.activateSelectedSettingGroup(3));
        CHECK(waitFor([&] { return !controller.operationBusy(); }, 6'000));
        CHECK(controller.lastError().isEmpty());
        CHECK(fixture.active() == 3U);
        CHECK(fixture.writeCount() == 1U);
        CHECK(controller.selectedSettingGroup()
                  .value(QStringLiteral("actSG")).toULongLong() == 3U);

        CHECK(!controller.activateSelectedSettingGroup(5));
        CHECK(fixture.writeCount() == 1U);

        fixture.setEdit(2U);
        CHECK(controller.refreshSettingGroups());
        CHECK(waitFor([&] { return !controller.operationBusy(); }, 6'000));
        CHECK(controller.selectedSettingGroup()
                  .value(QStringLiteral("editSG")).toULongLong() == 2U);
        CHECK(!controller.selectedSettingGroup()
                   .value(QStringLiteral("canActivate")).toBool());
        CHECK(!controller.activateSelectedSettingGroup(2));
        CHECK(fixture.writeCount() == 1U);
        CHECK(fixture.acceptedConnections() == 1);

        std::cout
            << "FILE_SETTINGS_WORKSPACE_PASS"
            << " file_pages=2 file_entries=3 download_bytes=6 file_close=pass"
            << " sgcb=1 attributes=5 actsg_verified=3 activation_writes=1"
            << " persistent_association=pass full_sg_edit_claimed=false\n";
        std::cout
            << "FILE_SETTINGS_WORKSPACE_NEGATIVE_PASS"
            << " disconnected_ops=rejected invalid_group=rejected"
            << " edit_in_progress=rejected sink_failure_cleanup=pass"
            << " bounded_pages=4 bounded_entries=8 no_activation_retry=true\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FILE_SETTINGS_WORKSPACE_FAIL " << exception.what() << '\n';
        return 1;
    }
}
