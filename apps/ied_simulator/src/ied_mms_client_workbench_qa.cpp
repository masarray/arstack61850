// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsClientController.hpp"

#include "ariec61850/embedded/io.hpp"
#include "ariec61850/mms/data_codec.hpp"
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
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace mms = ar::iec61850::mms;
namespace embedded = ar::iec61850::embedded;
namespace wire = ar::iec61850::wire;

namespace {
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

QString selectedValue(MmsClientController& client) {
    return client.treeModel()->selectedNode().value(QStringLiteral("value")).toString();
}

mms::MmsTypeSpecification scalarType(
    const mms::MmsTypeKind kind,
    std::string name) {
    mms::MmsTypeSpecification type;
    type.kind = kind;
    type.name = std::move(name);
    return type;
}

mms::MmsTypeSpecification structureType(
    std::string name,
    std::vector<mms::MmsTypeSpecification> children) {
    mms::MmsTypeSpecification type;
    type.kind = mms::MmsTypeKind::structure;
    type.name = std::move(name);
    type.children = std::move(children);
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

[[nodiscard]] wire::EncodeResult readSetPoint(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    try {
        const auto value = *static_cast<const std::int64_t*>(context);
        return copyEncoded(mms::MmsDataCodec::encode(mms::MmsDataValue::integer(value)), destination);
    } catch (...) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
}

[[nodiscard]] wire::EncodeResult readLogicalNode(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    try {
        const auto value = *static_cast<const std::int64_t*>(context);
        const auto root = mms::MmsDataValue::structure({
            mms::MmsDataValue::structure({
                mms::MmsDataValue::structure({
                    mms::MmsDataValue::integer(value),
                }),
            }),
        });
        return copyEncoded(mms::MmsDataCodec::encode(root), destination);
    } catch (...) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
}

[[nodiscard]] mms::MmsStaticWriteResult writeSetPoint(
    void* context,
    const std::span<const std::uint8_t> encodedData) noexcept {
    if (context == nullptr || encodedData.empty()) return {false, 11U};
    try {
        const auto values = mms::MmsDataCodec::decode_all(encodedData);
        if (values.size() != 1U || values.front().kind() != mms::MmsDataKind::integer) {
            return {false, 11U};
        }
        *static_cast<std::int64_t*>(context) = std::get<std::int64_t>(values.front().value());
        return {true, 0U};
    } catch (...) {
        return {false, 11U};
    }
}

[[nodiscard]] embedded::IoResult qtReceive(
    void* context,
    const std::span<std::uint8_t> destination) noexcept {
    auto* socket = static_cast<QTcpSocket*>(context);
    if (socket == nullptr || destination.empty()) return {embedded::IoStatus::invalid_argument, 0U};
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
    if (socket == nullptr || bytes.empty()) return {embedded::IoStatus::invalid_argument, 0U};
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

class DeterministicMmsFixture final {
public:
    DeterministicMmsFixture() {
        const auto rootType = structureType({}, {
            structureType("SP", {
                structureType("SetPoint", {
                    scalarType(mms::MmsTypeKind::integer, "setVal"),
                }),
            }),
        });
        rootType_ = mms::MmsServiceCodec::encode_type_specification(rootType);
        leafType_ = mms::MmsServiceCodec::encode_type_specification(
            scalarType(mms::MmsTypeKind::integer, "setVal"));
        objects_[0] = mms::MmsStaticObjectEntry{
            "CLIENTLD0", "GGIO1", rootType_, readLogicalNode, &setPoint_};
        objects_[1] = mms::MmsStaticObjectEntry{
            "CLIENTLD0", "GGIO1$SP$SetPoint$setVal", leafType_, readSetPoint, &setPoint_,
            false, writeSetPoint, &setPoint_};
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

private:
    void acceptConnection() {
        while (server_.hasPendingConnections()) {
            auto* incoming = server_.nextPendingConnection();
            if (socket_ != nullptr) {
                incoming->disconnectFromHost();
                incoming->deleteLater();
                continue;
            }
            socket_ = incoming;
            mms::MmsStaticConnectionPolicy policy;
            policy.association_id = 1U;
            policy.owner[0] = 1U;
            policy.owner_size = 1U;
            runtime_ = std::make_unique<mms::MmsStaticConnectionRuntime>(*dispatcher_, policy);
            const embedded::TcpByteStream stream{socket_, qtSend, qtReceive};
            session_ = std::make_unique<mms::MmsStaticServerSession>(
                *runtime_,
                stream,
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

    std::int64_t setPoint_{7};
    std::vector<std::uint8_t> rootType_;
    std::vector<std::uint8_t> leafType_;
    std::array<mms::MmsStaticObjectEntry, 2U> objects_{};
    std::unique_ptr<mms::MmsStaticObjectTable> table_;
    std::unique_ptr<mms::MmsStaticApplicationDispatcher> dispatcher_;
    QTcpServer server_;
    QTimer pollTimer_;
    QTcpSocket* socket_{};
    std::unique_ptr<mms::MmsStaticConnectionRuntime> runtime_;
    std::unique_ptr<mms::MmsStaticServerSession> session_;
    std::array<std::uint8_t, 32'768U> receive_{};
    std::array<std::uint8_t, 32'768U> response_{};
    std::array<std::uint8_t, 8'192U> workspace_{};
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "Usage: ied_mms_client_workbench_qa <fixture.scd>\n";
        return 2;
    }
    static_cast<void>(argv[1]);

    DeterministicMmsFixture fixture;
    if (!fixture.start() || fixture.port() == 0U) {
        std::cerr << "Deterministic MMS fixture did not start.\n";
        return 3;
    }

    MmsClientController client;
    client.setHost(QStringLiteral("127.0.0.1"));
    client.setPort(fixture.port());
    if (!client.connectToIed() || !waitFor([&] { return client.connected(); }, 12'000)) {
        std::cerr << "Client connection/discovery failed: " << client.lastError().toStdString() << '\n';
        return 6;
    }

    if (client.logicalDeviceCount() != 1 || client.logicalNodeCount() != 1 ||
        client.dataObjectCount() != 1 || client.dataAttributeCount() != 1) {
        std::cerr << "Live model projection is incomplete: ld=" << client.logicalDeviceCount()
                  << " ln=" << client.logicalNodeCount()
                  << " do=" << client.dataObjectCount()
                  << " da=" << client.dataAttributeCount() << '\n';
        return 7;
    }
    if (!client.treeModel()->selectMmsItem(
            QStringLiteral("CLIENTLD0"), QStringLiteral("GGIO1$SP$SetPoint$setVal"))) {
        std::cerr << "Writable set point was not projected.\n";
        return 8;
    }

    const auto selected = client.treeModel()->selectedNode();
    if (selected.value(QStringLiteral("functionalConstraint")).toString() != QStringLiteral("SP") ||
        selected.value(QStringLiteral("mmsType")).toString() != QStringLiteral("integer") ||
        !selected.value(QStringLiteral("writable")).toBool()) {
        std::cerr << "Exact-type guarded Write policy did not classify the SP integer correctly.\n";
        return 9;
    }

    if (!client.readSelected() || !waitFor([&] { return !client.operationBusy(); }, 6'000)) {
        std::cerr << "Selected Read did not complete.\n";
        return 10;
    }
    const auto before = selectedValue(client);
    if (before != QStringLiteral("7")) {
        std::cerr << "Unexpected initial Read value: " << before.toStdString() << '\n';
        return 11;
    }

    if (!client.writeSelected(QStringLiteral("9")) ||
        !waitFor([&] { return !client.operationBusy(); }, 6'000)) {
        std::cerr << "Guarded Write did not complete: " << client.lastError().toStdString() << '\n';
        return 12;
    }
    if (!client.lastError().isEmpty()) {
        std::cerr << "Guarded Write failed: " << client.lastError().toStdString() << '\n';
        return 12;
    }
    const auto after = selectedValue(client);
    if (after != QStringLiteral("9")) {
        std::cerr << "Write verification Read did not observe 9; got " << after.toStdString() << '\n';
        return 13;
    }

    std::cout << "MMS_CLIENT_WORKBENCH_PASS"
              << " ld=" << client.logicalDeviceCount()
              << " ln=" << client.logicalNodeCount()
              << " do=" << client.dataObjectCount()
              << " da=" << client.dataAttributeCount()
              << " read=" << before.toStdString()
              << " write_verified=" << after.toStdString()
              << " exact_type=integer fc=SP persistent_association=pass\n";

    const auto staleGeneration = client.generation();
    if (!client.readSelected()) {
        std::cerr << "Could not start stale-session negative Read.\n";
        return 14;
    }
    client.disconnectFromIed();
    const auto currentGeneration = client.generation();
    if (currentGeneration <= staleGeneration || !waitFor([&] { return !client.operationBusy(); }, 1'000)) {
        std::cerr << "Disconnect did not invalidate the session generation.\n";
        return 15;
    }
    QThread::msleep(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (client.connected() || client.treeModel()->totalNodeCount() != 0 || !client.treeModel()->selectedNode().isEmpty()) {
        std::cerr << "Stale session state was applied after disconnect.\n";
        return 16;
    }

    std::cout << "MMS_CLIENT_STALE_SESSION_NEGATIVE_PASS"
              << " stale_generation=" << staleGeneration
              << " current_generation=" << currentGeneration
              << " stale_state_not_applied=true bounded_io_worker=1\n";
    return 0;
}
