// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/goose/frame.hpp"
#include "ariec61850/goose/raw_ethernet_subscriber.hpp"
#include "ariec61850/goose/subscriber_supervisor.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

class QTimer;

class GooseMonitorController : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList interfaces READ interfaces NOTIFY monitorChanged)
    Q_PROPERTY(QString interfaceName READ interfaceName WRITE setInterfaceName NOTIFY monitorChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY monitorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY monitorChanged)
    Q_PROPERTY(int streamCount READ streamCount NOTIFY monitorChanged)
    Q_PROPERTY(quint64 packetCount READ packetCount NOTIFY monitorChanged)
    Q_PROPERTY(quint64 decodeErrorCount READ decodeErrorCount NOTIFY monitorChanged)
    Q_PROPERTY(quint64 rejectedFrameCount READ rejectedFrameCount NOTIFY monitorChanged)
    Q_PROPERTY(quint64 droppedStreamCount READ droppedStreamCount NOTIFY monitorChanged)
    Q_PROPERTY(quint64 duplicateCount READ duplicateCount NOTIFY monitorChanged)
    Q_PROPERTY(quint64 sequenceIssueCount READ sequenceIssueCount NOTIFY monitorChanged)
    Q_PROPERTY(quint64 timeoutCount READ timeoutCount NOTIFY monitorChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedStream READ selectedStream NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedValues READ selectedValues NOTIFY selectionChanged)
    Q_PROPERTY(QStringList recentEvents READ recentEvents NOTIFY monitorChanged)
    Q_PROPERTY(int retainedPacketCount READ retainedPacketCount NOTIFY monitorChanged)
    Q_PROPERTY(int streamCapacity READ streamCapacity CONSTANT)
    Q_PROPERTY(int memberCapacity READ memberCapacity CONSTANT)
    Q_PROPERTY(int eventCapacity READ eventCapacity CONSTANT)
    Q_PROPERTY(int retainedPacketCapacity READ retainedPacketCapacity CONSTANT)

public:
    using clock = ar::iec61850::goose::GooseSubscriberSupervisor::clock;

    enum Role {
        SourceMacRole = Qt::UserRole + 1,
        DestinationMacRole,
        AppIdRole,
        AppIdTextRole,
        VlanIdRole,
        VlanPriorityRole,
        GoIdRole,
        GoCbRefRole,
        DataSetReferenceRole,
        ConfRevRole,
        StateNumberRole,
        SequenceNumberRole,
        TtlRole,
        StatusRole,
        TimedOutRole,
        LateRole,
        AnomalyRole,
        PacketCountRole,
        LastSeenMsRole,
        ValuesSummaryRole,
        ValueCountRole,
    };

    explicit GooseMonitorController(QObject* parent = nullptr);
    ~GooseMonitorController() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QStringList interfaces() const;
    [[nodiscard]] QString interfaceName() const { return interfaceName_; }
    void setInterfaceName(const QString& value);
    [[nodiscard]] bool capturing() const noexcept { return capturing_; }
    [[nodiscard]] QString statusText() const { return statusText_; }
    [[nodiscard]] int streamCount() const noexcept { return static_cast<int>(streams_.size()); }
    [[nodiscard]] quint64 packetCount() const noexcept { return packetCount_; }
    [[nodiscard]] quint64 decodeErrorCount() const noexcept { return decodeErrorCount_; }
    [[nodiscard]] quint64 rejectedFrameCount() const noexcept { return rejectedFrameCount_; }
    [[nodiscard]] quint64 droppedStreamCount() const noexcept { return droppedStreamCount_; }
    [[nodiscard]] quint64 duplicateCount() const noexcept { return duplicateCount_; }
    [[nodiscard]] quint64 sequenceIssueCount() const noexcept { return sequenceIssueCount_; }
    [[nodiscard]] quint64 timeoutCount() const noexcept { return timeoutCount_; }
    [[nodiscard]] int selectedRow() const noexcept { return selectedRow_; }
    [[nodiscard]] QVariantMap selectedStream() const;
    [[nodiscard]] QStringList selectedValues() const;
    [[nodiscard]] QStringList recentEvents() const { return recentEvents_; }
    [[nodiscard]] int retainedPacketCount() const noexcept {
        return static_cast<int>(retainedPackets_.size());
    }
    [[nodiscard]] int streamCapacity() const noexcept { return kStreamCapacity; }
    [[nodiscard]] int memberCapacity() const noexcept { return kMemberCapacity; }
    [[nodiscard]] int eventCapacity() const noexcept { return kEventCapacity; }
    [[nodiscard]] int retainedPacketCapacity() const noexcept { return kRetainedPacketCapacity; }

    Q_INVOKABLE bool startCapture();
    Q_INVOKABLE void stopCapture();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE bool exportPcap(const QUrl& fileUrl);

    // Deterministic QA/offline evidence path. This uses the same decoder,
    // supervisor, bounded model and retention path as live raw-Ethernet capture.
    [[nodiscard]] bool ingestFrameBytes(
        std::span<const std::uint8_t> frameBytes,
        clock::time_point arrivalTime);
    void checkTimeoutsAt(clock::time_point now);

signals:
    void monitorChanged();
    void selectionChanged();

private:
    struct StreamSlot final {
        QString key;
        ar::iec61850::goose::GooseFrame frame;
        ar::iec61850::goose::GooseSubscriberSupervisor supervisor;
        QString status;
        QStringList values;
        QString valuesSummary;
        quint64 packetCount{};
        qint64 lastSeenMilliseconds{};
        bool timedOut{};
        bool late{};
        bool anomaly{};
    };

    static constexpr int kStreamCapacity = 256;
    static constexpr int kMemberCapacity = 256;
    static constexpr int kEventCapacity = 256;
    static constexpr int kRetainedPacketCapacity = 4096;
    static constexpr int kPollFrameBudget = 64;
    static constexpr int kPollIntervalMilliseconds = 10;
    static constexpr int kExpiryScanIntervalMilliseconds = 100;

    [[nodiscard]] QVariantMap streamMap(const StreamSlot& slot) const;
    [[nodiscard]] static QString sequenceStatusText(ar::iec61850::goose::GooseSequenceStatus status);
    [[nodiscard]] static QString formatValue(const ar::iec61850::mms::MmsDataValue& value, int depth = 0);
    [[nodiscard]] static QString streamKey(const ar::iec61850::goose::GooseFrame& frame);
    [[nodiscard]] int indexForKey(const QString& key) const noexcept;
    bool ingestFrameInternal(
        std::span<const std::uint8_t> frameBytes,
        clock::time_point arrivalTime,
        std::chrono::system_clock::time_point captureTimestamp);
    void pollCapture();
    void appendEvent(QString event);
    void retainPacket(
        std::span<const std::uint8_t> frameBytes,
        std::chrono::system_clock::time_point timestamp);
    void failCapture(const QString& reason);

    std::vector<StreamSlot> streams_;
    std::deque<ar::iec61850::capture::PcapPacket> retainedPackets_;
    QStringList recentEvents_;
    QString interfaceName_;
    QString statusText_{QStringLiteral("Stopped")};
    ar::iec61850::goose::RawEthernetSubscriber transport_;
    QTimer* captureTimer_{};
    QElapsedTimer monitorClock_;
    qint64 lastExpiryScanMilliseconds_{};
    int selectedRow_{-1};
    bool capturing_{};
    quint64 packetCount_{};
    quint64 decodeErrorCount_{};
    quint64 rejectedFrameCount_{};
    quint64 droppedStreamCount_{};
    quint64 duplicateCount_{};
    quint64 sequenceIssueCount_{};
    quint64 timeoutCount_{};
};
