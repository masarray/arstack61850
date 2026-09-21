// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedBrowserSessionController.hpp"

#include <QChar>

namespace {
bool hasActiveReportSession(const MmsReportController* reports) {
    return reports && (reports->connected() || reports->busy() || reports->cleanupRequired());
}

bool hasActiveUtilitySession(const MmsFileSettingsController* utilities) {
    return utilities && (utilities->connected() || utilities->busy());
}

bool hasActiveControlSession(const MmsControlController* controls) {
    return controls && (controls->connected() || controls->busy());
}
} // namespace

IedBrowserSessionController::IedBrowserSessionController(QObject* parent)
    : QObject(parent) {}

QString IedBrowserSessionController::normalizedHost(const QString& value) {
    auto host = value.trimmed();
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']')) && host.size() > 2) {
        host = host.mid(1, host.size() - 2);
    }
    if (host.isEmpty() || host.size() > 255) return {};
    for (const auto character : host) {
        if (character.isSpace() || character.category() == QChar::Other_Control) return {};
    }
    return host;
}

void IedBrowserSessionController::connectServiceSignals(QObject* service) {
    if (!service) return;
    connect(service, SIGNAL(stateChanged()), this, SIGNAL(stateChanged()), Qt::UniqueConnection);
}

void IedBrowserSessionController::setClient(MmsClientController* value) {
    if (client_ == value) return;
    if (client_) disconnect(client_, nullptr, this, nullptr);
    client_ = value;
    connectServiceSignals(client_);
    syncConfiguration();
    emit servicesChanged();
    emit stateChanged();
}

void IedBrowserSessionController::setReports(MmsReportController* value) {
    if (reports_ == value) return;
    if (reports_) disconnect(reports_, nullptr, this, nullptr);
    reports_ = value;
    connectServiceSignals(reports_);
    syncConfiguration();
    emit servicesChanged();
    emit stateChanged();
}

void IedBrowserSessionController::setControls(MmsControlController* value) {
    if (controls_ == value) return;
    if (controls_) disconnect(controls_, nullptr, this, nullptr);
    controls_ = value;
    connectServiceSignals(controls_);
    syncConfiguration();
    emit servicesChanged();
    emit stateChanged();
}

void IedBrowserSessionController::setEngineeringContext(IedEngineeringContextController* value) {
    if (engineeringContext_ == value) return;
    if (engineeringContext_) disconnect(engineeringContext_, nullptr, this, nullptr);
    engineeringContext_ = value;
    if (engineeringContext_) {
        connect(
            engineeringContext_,
            &IedEngineeringContextController::contextChanged,
            this,
            [this] {
                if (!configurationLocked()) applyEngineeringEndpoint();
                emit stateChanged();
            });
    }
    syncConfiguration();
    if (!configurationLocked()) applyEngineeringEndpoint();
    emit servicesChanged();
    emit stateChanged();
}

void IedBrowserSessionController::setUtilities(MmsFileSettingsController* value) {
    if (utilities_ == value) return;
    if (utilities_) disconnect(utilities_, nullptr, this, nullptr);
    utilities_ = value;
    connectServiceSignals(utilities_);
    syncConfiguration();
    emit servicesChanged();
    emit stateChanged();
}

bool IedBrowserSessionController::connected() const noexcept {
    return client_ && client_->connected();
}

bool IedBrowserSessionController::busy() const noexcept {
    return (client_ && client_->busy()) ||
           (reports_ && reports_->busy()) ||
           (utilities_ && utilities_->busy()) ||
           (controls_ && controls_->busy());
}

bool IedBrowserSessionController::configurationLocked() const noexcept {
    return connected() || busy() || hasActiveReportSession(reports_) || hasActiveUtilitySession(utilities_) || hasActiveControlSession(controls_);
}

QString IedBrowserSessionController::endpoint() const {
    const auto displayHost = host_.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(host_)
        : host_;
    return QStringLiteral("%1:%2").arg(displayHost).arg(port_);
}

QString IedBrowserSessionController::stateText() const {
    if (!coordinatorError_.isEmpty()) return QStringLiteral("Configuration error");
    if (!client_) return QStringLiteral("Browser unavailable");
    if (reports_ && reports_->cleanupRequired()) return QStringLiteral("Report cleanup required");
    if (client_->connected()) {
        if ((reports_ && reports_->busy()) || (utilities_ && utilities_->busy()) ||
            (controls_ && controls_->busy())) {
            return QStringLiteral("Connected · opening service");
        }
        return QStringLiteral("Connected");
    }
    return client_->stateText();
}

QString IedBrowserSessionController::lastError() const {
    if (!coordinatorError_.isEmpty()) return coordinatorError_;
    if (client_ && !client_->lastError().isEmpty()) return client_->lastError();
    if (reports_ && !reports_->lastError().isEmpty()) return reports_->lastError();
    if (utilities_ && !utilities_->lastError().isEmpty()) return utilities_->lastError();
    if (controls_ && !controls_->lastError().isEmpty()) return controls_->lastError();
    return {};
}

void IedBrowserSessionController::setHost(const QString& value) {
    const auto normalized = normalizedHost(value);
    if (normalized.isEmpty()) {
        coordinatorError_ = QStringLiteral("A non-empty host without whitespace/control characters is required.");
        emit stateChanged();
        return;
    }
    if (host_ == normalized) return;
    if (configurationLocked()) {
        coordinatorError_ = QStringLiteral("Disconnect the IED Browser before changing the endpoint.");
        emit stateChanged();
        return;
    }
    host_ = normalized;
    coordinatorError_.clear();
    syncConfiguration();
    emit configurationChanged();
    emit stateChanged();
}

void IedBrowserSessionController::setPort(const int value) {
    if (value < 1 || value > 65'535) {
        coordinatorError_ = QStringLiteral("TCP port must be in range 1..65535.");
        emit stateChanged();
        return;
    }
    if (port_ == value) return;
    if (configurationLocked()) {
        coordinatorError_ = QStringLiteral("Disconnect the IED Browser before changing the endpoint.");
        emit stateChanged();
        return;
    }
    port_ = value;
    coordinatorError_.clear();
    syncConfiguration();
    emit configurationChanged();
    emit stateChanged();
}

void IedBrowserSessionController::setTrustedSclPath(const QString& value) {
    const auto normalized = value.trimmed();
    if (trustedSclPath_ == normalized) return;
    trustedSclPath_ = normalized;
    coordinatorError_.clear();
    syncConfiguration();
    emit configurationChanged();
}

void IedBrowserSessionController::syncConfiguration() {
    if (client_) {
        client_->setHost(host_);
        client_->setPort(port_);
        client_->setTrustedSclPath(trustedSclPath_);
        client_->setEngineeringContext(engineeringContext_);
    }
    if (reports_) {
        reports_->setHost(host_);
        reports_->setPort(port_);
        reports_->setEngineeringContext(engineeringContext_);
    }
    if (utilities_) {
        utilities_->setHost(host_);
        utilities_->setPort(port_);
        utilities_->setEngineeringContext(engineeringContext_);
    }
    if (controls_) {
        controls_->setHost(host_);
        controls_->setPort(port_);
        controls_->setEngineeringContext(engineeringContext_);
    }
}

bool IedBrowserSessionController::applyEngineeringEndpoint() {
    if (!engineeringContext_ || !engineeringContext_->loaded() ||
        engineeringContext_->selectionRequired() || configurationLocked()) {
        return false;
    }
    const auto host = engineeringContext_->endpointHost().trimmed();
    const auto port = engineeringContext_->endpointPort();
    if (host.isEmpty() || port < 1 || port > 65'535) return false;
    setHost(host);
    setPort(port);
    return host_ == host && port_ == port;
}

bool IedBrowserSessionController::connectUsingEngineeringContext() {
    if (!engineeringContext_ || !engineeringContext_->loaded()) {
        coordinatorError_ = QStringLiteral("Open or discover an IED model before connecting this Browser context.");
        emit stateChanged();
        return false;
    }
    if (engineeringContext_->selectionRequired()) {
        coordinatorError_ = QStringLiteral("Select the active IED from the engineering model before connecting.");
        emit stateChanged();
        return false;
    }
    if (configurationLocked()) return connected();

    if (engineeringContext_->authorityKey() == QStringLiteral("scl")) {
        const auto source = engineeringContext_->sourcePath().trimmed();
        if (source.isEmpty()) {
            coordinatorError_ = QStringLiteral("The active SCL context has no trusted local source path.");
            emit stateChanged();
            return false;
        }
        setTrustedSclPath(source);
    } else {
        setTrustedSclPath({});
    }
    static_cast<void>(applyEngineeringEndpoint());
    return connectToIed();
}

bool IedBrowserSessionController::discoverAndConnect() {
    if (configurationLocked()) return connected();
    setTrustedSclPath({});
    return connectToIed();
}

bool IedBrowserSessionController::connectToIed() {
    if (!client_) {
        coordinatorError_ = QStringLiteral("IED Browser model service is unavailable.");
        emit stateChanged();
        return false;
    }
    if (normalizedHost(host_).isEmpty() || port_ < 1 || port_ > 65'535) {
        coordinatorError_ = QStringLiteral("A valid IED Browser endpoint is required.");
        emit stateChanged();
        return false;
    }
    if (connected()) return true;
    if (busy()) return false;

    // P0 keeps proven service associations separate internally. A new logical
    // Browser connect always starts from a clean auxiliary-service state.
    if (reports_ && (reports_->connected() || reports_->cleanupRequired())) {
        reports_->disconnectFromIed();
    }
    if (utilities_ && utilities_->connected()) {
        utilities_->disconnectFromIed();
    }
    if (controls_ && (controls_->connected() || controls_->busy())) {
        controls_->disconnectFromIed();
    }
    syncConfiguration();
    coordinatorError_.clear();
    const auto started = client_->connectToIed();
    emit stateChanged();
    return started;
}

void IedBrowserSessionController::disconnectFromIed() {
    coordinatorError_.clear();
    if (reports_) reports_->disconnectFromIed();
    if (utilities_) utilities_->disconnectFromIed();
    if (controls_) controls_->disconnectFromIed();
    if (client_) client_->disconnectFromIed();
    emit stateChanged();
}

bool IedBrowserSessionController::ensureReportsConnected() {
    if (!connected() || !reports_) return false;
    if (reports_->connected()) return true;
    if (reports_->busy() || reports_->cleanupRequired()) return false;
    syncConfiguration();
    return reports_->connectToIed();
}

bool IedBrowserSessionController::ensureUtilitiesConnected() {
    if (!connected() || !utilities_) return false;
    if (utilities_->connected()) return true;
    if (utilities_->busy()) return false;
    syncConfiguration();
    return utilities_->connectToIed();
}

bool IedBrowserSessionController::prepareControlObject(const QString& objectReference) {
    if (!connected() || !controls_) return false;
    if (controls_->busy()) return false;
    syncConfiguration();
    return controls_->prepareObject(objectReference);
}
