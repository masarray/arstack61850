// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

class SvCaptureController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList interfaces READ interfaces NOTIFY interfacesChanged)
    Q_PROPERTY(int suggestedInterface READ suggestedInterface NOTIFY interfacesChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY stateChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY stateChanged)
    Q_PROPERTY(QString outputFolder READ outputFolder NOTIFY statusChanged)
    Q_PROPERTY(QString lastReport READ lastReport NOTIFY statusChanged)
public:
    explicit SvCaptureController(QObject* parent = nullptr);
    ~SvCaptureController() override;

    [[nodiscard]] QStringList interfaces() const { return interfaceLabels_; }
    [[nodiscard]] int suggestedInterface() const { return suggestedInterface_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] bool capturing() const { return capture_.state() != QProcess::NotRunning; }
    [[nodiscard]] bool processing() const { return decoder_.isRunning(); }
    [[nodiscard]] QString outputFolder() const { return outputFolder_; }
    [[nodiscard]] QString lastReport() const { return lastReport_; }

    Q_INVOKABLE void refreshInterfaces();
    Q_INVOKABLE bool startCapture(int index, int seconds);
    Q_INVOKABLE void abortCapture();
    Q_INVOKABLE void openOutputFolder();

signals:
    void interfacesChanged();
    void statusChanged();
    void stateChanged();

private:
    void setStatus(const QString& text);
    void onCaptureFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void decodeCapturedFile();
    static QVariantMap decodePcap(const QString& pcapFile, const QString& csvFile,
                                 const QString& secondsFile, const QString& jsonFile,
                                 int requestedSeconds, const QString& adapter);
    QProcess discovery_;
    QProcess capture_;
    QFutureWatcher<QVariantMap> decoder_;
    QStringList interfaceLabels_;
    QStringList interfaceIds_;
    int suggestedInterface_{-1};
    QString status_{QStringLiteral("Discover an Ethernet capture adapter to begin.")};
    QString outputFolder_;
    QString lastReport_;
    QString activePcap_;
    QString activeCsv_;
    QString activeSeconds_;
    QString activeJson_;
    QString activeAdapter_;
    QString dumpcap_;
    int activeSecondsRequested_{0};
    bool abortRequested_{false};
};
