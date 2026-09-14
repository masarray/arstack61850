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
#include <string>
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
        return copyEncoded(
            mms::MmsDataCodec::encode(mms::MmsDataValue::integer(value)),
            destination);
    } catch (...) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
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

class SclAssistedFixture final {
public:
    SclAssistedFixture() {
        leafType_ = mms::MmsServiceCodec::encode_type_specification(
            scalarType(mms::MmsTypeKind::integer, "setVal"));
        objects_[0] = mms::MmsStaticObjectEntry{
            "CLIENTLD0",
            "GGIO1$SP$SetPoint$setVal",
            leafType_,
            readSetPoint,
            &setPoint_};
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
    [[nodiscard]] bool connectionIdle() const noexcept {
        return socket_ == nullptr && session_ == nullptr && runtime_ == nullptr;
    }

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

    std::int64_t setPoint_{7};
    std::vector<std::uint8_t> leafType_;
    std::array<mms::MmsStaticObjectEntry, 1U> objects_{};
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

[[nodiscard]] bool select(
    MmsClientController& client,
    const QString& item) {
    return client.treeModel()->selectMmsItem(QStringLiteral("CLIENTLD0"), item);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "Usage: ied_mms_scl_assisted_workbench_qa <trusted.scd>\n";
        return 2;
    }

    SclAssistedFixture fixture;
    if (!fixture.start() || fixture.port() == 0U) {
        std::cerr << "SCL-assisted MMS fixture did not start.\n";
        return 3;
    }

    MmsClientController client;
    client.setHost(QStringLiteral("127.0.0.1"));
    client.setPort(fixture.port());
    client.setTrustedSclPath(QString::fromLocal8Bit(argv[1]));
    if (!client.trustedSclAvailable()) {
        std::cerr << "Trusted SCL source was not armed.\n";
        return 4;
    }
    if (!client.connectToIed() || !waitFor([&] { return client.connected(); }, 12'000)) {
        std::cerr << "Trusted-SCL connection failed: " << client.lastError().toStdString() << '\n';
        return 5;
    }

    if (client.modelSource() != QStringLiteral("Trusted SCL + initial snapshot")) {
        std::cerr << "Workbench silently selected the wrong online-model path: "
                  << client.modelSource().toStdString() << '\n';
        return 6;
    }
    if (client.logicalDeviceCount() != 1 || client.logicalNodeCount() != 2 ||
        client.dataObjectCount() != 2 || client.dataAttributeCount() != 4) {
        std::cerr << "Trusted SCL topology was not preserved: ld=" << client.logicalDeviceCount()
                  << " ln=" << client.logicalNodeCount()
                  << " do=" << client.dataObjectCount()
                  << " da=" << client.dataAttributeCount() << '\n';
        return 7;
    }

    if (!select(client, QStringLiteral("GGIO1$SP$SetPoint$setVal"))) {
        std::cerr << "Trusted SCL set-point leaf was not projected.\n";
        return 8;
    }
    const auto sp = client.treeModel()->selectedNode();
    if (sp.value(QStringLiteral("value")).toString() != QStringLiteral("7") ||
        sp.value(QStringLiteral("functionalConstraint")).toString() != QStringLiteral("SP") ||
        sp.value(QStringLiteral("mmsType")).toString() != QStringLiteral("integer")) {
        std::cerr << "Initial FC-root snapshot did not populate the trusted SP leaf.\n";
        return 9;
    }

    // The fixture deliberately omits the SCL-declared ST root. The topology must
    // stay present while its live value remains unavailable; it must not be
    // deleted or shifted onto a neighbouring SCL leaf.
    if (!select(client, QStringLiteral("GGIO1$ST$Status$stVal"))) {
        std::cerr << "Missing online FC root corrupted the trusted SCL topology.\n";
        return 10;
    }
    const auto st = client.treeModel()->selectedNode();
    if (!st.value(QStringLiteral("value")).toString().isEmpty() ||
        st.value(QStringLiteral("functionalConstraint")).toString() != QStringLiteral("ST")) {
        std::cerr << "Missing online FC root was not kept unavailable/fail-closed.\n";
        return 11;
    }

    const auto diagnostics = client.diagnosticsText();
    if (!diagnostics.contains(QStringLiteral("SCL synchronization complete")) ||
        !diagnostics.contains(QStringLiteral("1 Read(s)")) ||
        !diagnostics.contains(QStringLiteral("1 mapped leaf value(s)"))) {
        std::cerr << "SCL-assisted path did not expose deterministic snapshot diagnostics.\n";
        return 12;
    }

    // Prove that synchronize_scl() leaves the same association usable by normal
    // workbench operations after the initial snapshot.
    if (!select(client, QStringLiteral("GGIO1$SP$SetPoint$setVal")) ||
        !client.readSelected() ||
        !waitFor([&] { return !client.operationBusy(); }, 6'000) ||
        !client.lastError().isEmpty() ||
        client.treeModel()->selectedNode().value(QStringLiteral("value")).toString() !=
            QStringLiteral("7")) {
        std::cerr << "Association did not remain usable after the SCL snapshot: "
                  << client.lastError().toStdString() << '\n';
        return 13;
    }

    client.disconnectFromIed();
    if (!waitFor([&] { return fixture.connectionIdle() && !client.operationBusy(); }, 3'000)) {
        std::cerr << "Trusted-SCL session did not cleanly disconnect.\n";
        return 14;
    }

    // Clearing the engineering source is the explicit user-controlled fallback
    // to full network discovery. It must not happen silently while a trusted SCL
    // is armed.
    client.setTrustedSclPath({});
    if (client.trustedSclAvailable()) {
        std::cerr << "Trusted SCL source did not clear.\n";
        return 15;
    }
    if (!client.connectToIed() || !waitFor([&] { return client.connected(); }, 12'000)) {
        std::cerr << "Explicit live-discovery fallback failed: "
                  << client.lastError().toStdString() << '\n';
        return 16;
    }
    if (client.modelSource() != QStringLiteral("Live MMS discovery") ||
        client.logicalDeviceCount() != 1 || client.logicalNodeCount() != 1 ||
        client.dataObjectCount() != 1 || client.dataAttributeCount() != 1) {
        std::cerr << "Cleared SCL source did not restore the full-discovery path.\n";
        return 17;
    }

    client.disconnectFromIed();
    if (!waitFor([&] { return fixture.connectionIdle(); }, 3'000)) {
        std::cerr << "Live-discovery fallback session did not cleanly disconnect.\n";
        return 18;
    }

    // A configured but invalid engineering source is an explicit fault. The app
    // must not hide that failure by silently switching to a different model.
    client.setTrustedSclPath(QString::fromLocal8Bit(argv[1]) + QStringLiteral(".missing"));
    if (!client.connectToIed() ||
        !waitFor([&] { return !client.busy(); }, 6'000) ||
        client.connected() || client.lastError().isEmpty()) {
        std::cerr << "Invalid trusted SCL did not fail closed.\n";
        return 19;
    }
    if (client.modelSource() == QStringLiteral("Live MMS discovery")) {
        std::cerr << "Invalid trusted SCL silently fell back to live discovery.\n";
        return 20;
    }

    std::cout << "MMS_SCL_ASSISTED_WORKBENCH_PASS"
              << " trusted_path=pass"
              << " domain_validation=pass"
              << " initial_snapshot=pass"
              << " mapped_leaf=1"
              << " missing_fc_preserved=true"
              << " persistent_association=pass"
              << " explicit_fallback=pass"
              << " invalid_scl_fail_closed=true\n";
    return 0;
}
