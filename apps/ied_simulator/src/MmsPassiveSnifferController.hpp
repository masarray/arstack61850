// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/capture/passive_mms.hpp"
#include "ariec61850/capture/raw_mms_packet_receiver.hpp"

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class MmsPassiveSnifferController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList interfaces READ interfaces CONSTANT)
    Q_PROPERTY(QString interfaceName READ interfaceName WRITE setInterfaceName NOTIFY stateChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY eventsChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QVariantMap counters READ counters NOTIFY eventsChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedEvent READ selectedEvent NOTIFY selectionChanged)

public:
    explicit MmsPassiveSnifferController(QObject* parent = nullptr);
    ~MmsPassiveSnifferController() override;

    [[nodiscard]] QStringList interfaces() const;
    [[nodiscard]] QString interfaceName() const { return interfaceName_; }
    void setInterfaceName(const QString& value);
    [[nodiscard]] bool capturing() const noexcept { return capturing_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] QString statusText() const { return statusText_; }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString filter() const { return filter_; }
    void setFilter(const QString& value);
    [[nodiscard]] QVariantList events() const;
    [[nodiscard]] QVariantMap counters() const;
    [[nodiscard]] int selectedIndex() const noexcept { return selectedIndex_; }
    [[nodiscard]] QVariantMap selectedEvent() const;

    Q_INVOKABLE bool startCapture();
    Q_INVOKABLE void stopCapture();
    Q_INVOKABLE bool importPcap(const QUrl& url);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void select(int index);

signals:
    void stateChanged();
    void eventsChanged();
    void selectionChanged();

private:
    static QVariantMap eventMap(const ar::iec61850::capture::PassiveMmsEvent& item);
    static QVariantMap counterMap(const ar::iec61850::capture::PassiveMmsCounters& item);
    void refreshLive();
    void failCapture(const QString& error);

    static constexpr int kPollIntervalMs = 10;
    static constexpr int kPollBudget = 64;
    static constexpr std::uintmax_t kMaxPcapBytes = 64U * 1024U * 1024U;
    static constexpr std::size_t kMaxPcapPackets = 100000U;

    ar::iec61850::capture::RawMmsPacketReceiver receiver_;
    ar::iec61850::capture::PassiveMmsDecoder decoder_;
    QTimer timer_;
    QThreadPool workerPool_;
    QVariantList rows_;
    QVariantMap counters_;
    QString interfaceName_;
    QString statusText_{QStringLiteral("Stopped")};
    QString lastError_;
    QString filter_{QStringLiteral("All")};
    bool capturing_{};
    bool busy_{};
    int selectedIndex_{-1};
    quint64 generation_{};
};
