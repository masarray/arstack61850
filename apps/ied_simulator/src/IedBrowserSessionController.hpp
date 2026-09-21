// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MmsClientController.hpp"
#include "MmsControlController.hpp"
#include "MmsFileSettingsController.hpp"
#include "MmsReportController.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class IedBrowserSessionController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY configurationChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY configurationChanged)
    Q_PROPERTY(QString trustedSclPath READ trustedSclPath WRITE setTrustedSclPath NOTIFY configurationChanged)
    Q_PROPERTY(MmsClientController* client READ client WRITE setClient NOTIFY servicesChanged)
    Q_PROPERTY(MmsReportController* reports READ reports WRITE setReports NOTIFY servicesChanged)
    Q_PROPERTY(MmsFileSettingsController* utilities READ utilities WRITE setUtilities NOTIFY servicesChanged)
    Q_PROPERTY(MmsControlController* controls READ controls WRITE setControls NOTIFY servicesChanged)
    Q_PROPERTY(IedEngineeringContextController* engineeringContext READ engineeringContext WRITE setEngineeringContext NOTIFY servicesChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool configurationLocked READ configurationLocked NOTIFY stateChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY configurationChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    explicit IedBrowserSessionController(QObject* parent = nullptr);

    [[nodiscard]] QString host() const { return host_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    [[nodiscard]] QString trustedSclPath() const { return trustedSclPath_; }
    [[nodiscard]] MmsClientController* client() const noexcept { return client_; }
    [[nodiscard]] MmsReportController* reports() const noexcept { return reports_; }
    [[nodiscard]] MmsFileSettingsController* utilities() const noexcept { return utilities_; }
    [[nodiscard]] MmsControlController* controls() const noexcept { return controls_; }
    [[nodiscard]] IedEngineeringContextController* engineeringContext() const noexcept {
        return engineeringContext_;
    }

    void setHost(const QString& value);
    void setPort(int value);
    void setTrustedSclPath(const QString& value);
    void setClient(MmsClientController* value);
    void setReports(MmsReportController* value);
    void setUtilities(MmsFileSettingsController* value);
    void setControls(MmsControlController* value);
    void setEngineeringContext(IedEngineeringContextController* value);

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool configurationLocked() const noexcept;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString lastError() const;

    Q_INVOKABLE bool applyEngineeringEndpoint();
    Q_INVOKABLE bool connectUsingEngineeringContext();
    Q_INVOKABLE bool discoverAndConnect();
    Q_INVOKABLE bool connectToIed();
    Q_INVOKABLE void disconnectFromIed();
    Q_INVOKABLE bool ensureReportsConnected();
    Q_INVOKABLE bool ensureUtilitiesConnected();
    Q_INVOKABLE bool prepareControlObject(const QString& objectReference);

signals:
    void configurationChanged();
    void servicesChanged();
    void stateChanged();

private:
    [[nodiscard]] static QString normalizedHost(const QString& value);
    void syncConfiguration();
    void connectServiceSignals(QObject* service);

    QString host_{QStringLiteral("127.0.0.1")};
    int port_{102};
    QString trustedSclPath_;
    QString coordinatorError_;
    QPointer<MmsClientController> client_;
    QPointer<MmsReportController> reports_;
    QPointer<MmsFileSettingsController> utilities_;
    QPointer<MmsControlController> controls_;
    QPointer<IedEngineeringContextController> engineeringContext_;
};
