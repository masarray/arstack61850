// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareWorker.hpp"

#include <QProcess>
#include <QThread>
#include <QTimer>

namespace {
constexpr int kShutdownWaitMs = 1500;
}

FirmwareWorker::FirmwareWorker(QObject* parent) : QObject(parent) {}

void FirmwareWorker::initialize() {
    if (initialized_) return;
    initialized_ = true;
    shuttingDown_ = false;

    process_ = new QProcess(this);
    launchTimer_ = new QTimer(this);
    operationTimer_ = new QTimer(this);

    process_->setProcessChannelMode(QProcess::SeparateChannels);
    launchTimer_->setSingleShot(true);
    operationTimer_->setSingleShot(true);

    connect(launchTimer_, &QTimer::timeout, this, [this] {
        if (!operationActive_ || process_ == nullptr || process_->state() != QProcess::Starting) return;
        // QProcess::kill() is asynchronous. Retain the operation/COM lease
        // until QProcess has confirmed NotRunning via finished/errorOccurred.
        killCause_ = KillCause::launchTimeout;
        if (operationTimer_ != nullptr) operationTimer_->stop();
        process_->kill();
        if (operationActive_ && process_->state() == QProcess::NotRunning)
            finishProcess(-1, false);
    });

    connect(operationTimer_, &QTimer::timeout, this, [this] {
        if (!operationActive_ || process_ == nullptr) return;
        killCause_ = KillCause::operationTimeout;
        if (launchTimer_ != nullptr) launchTimer_->stop();
        process_->kill();
        if (operationActive_ && process_->state() == QProcess::NotRunning)
            finishProcess(-1, false);
    });

    connect(process_, &QProcess::started, this, [this] {
        if (!operationActive_ || shuttingDown_) return;
        if (killCause_ != KillCause::none) { process_->kill(); return; }
        if (launchTimer_ != nullptr) launchTimer_->stop();
        if (operationTimer_ != nullptr) operationTimer_->start();
        emit operationStarted(generation_);
    });

    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        if (!operationActive_ || process_ == nullptr) return;
        const QString text = QString::fromUtf8(process_->readAllStandardOutput());
        if (!text.isEmpty()) emit outputReady(generation_, text);
    });
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        if (!operationActive_ || process_ == nullptr) return;
        const QString text = QString::fromUtf8(process_->readAllStandardError());
        if (!text.isEmpty()) emit outputReady(generation_, text);
    });

    connect(process_, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
        if (!operationActive_ || shuttingDown_ || process_ == nullptr || error != QProcess::FailedToStart) return;
        // A timeout during Starting can finish with FailedToStart rather than
        // finished(). Treat it as termination acknowledgement, not a new
        // operation result while the timed-out process still owns COM.
        if (killCause_ != KillCause::none) {
            if (process_->state() == QProcess::NotRunning) finishProcess(-1, false);
            return;
        }
        const quint64 generation = generation_;
        const QString message = process_->errorString();
        operationActive_ = false;
        cancelRequested_ = false;
        if (launchTimer_ != nullptr) launchTimer_->stop();
        if (operationTimer_ != nullptr) operationTimer_->stop();
        emit operationLaunchFailed(generation, message);
        clearOperation();
    });

    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](const int exitCode, const QProcess::ExitStatus status) {
        finishProcess(exitCode, status == QProcess::NormalExit);
    });

    emit ready(process_->thread() == QThread::currentThread() && thread() == QThread::currentThread());
}

void FirmwareWorker::startOperation(
    const quint64 generation,
    const QString& program,
    const QStringList& arguments,
    const QProcessEnvironment& environment,
    const int launchTimeoutMs,
    const int operationTimeoutMs) {
    if (!initialized_ || shuttingDown_ || process_ == nullptr) {
        emit operationRejected(generation, QStringLiteral("Firmware worker is not available."));
        return;
    }
    if (operationActive_ || process_->state() != QProcess::NotRunning) {
        emit operationRejected(generation, QStringLiteral("Another firmware process is already running."));
        return;
    }
    if (generation == 0 || program.trimmed().isEmpty() || launchTimeoutMs <= 0 || operationTimeoutMs <= 0) {
        emit operationRejected(generation, QStringLiteral("Invalid firmware operation request."));
        return;
    }

    generation_ = generation;
    operationActive_ = true;
    cancelRequested_ = false;
    killCause_ = KillCause::none;
    process_->setProcessEnvironment(environment);
    process_->setProgram(program);
    process_->setArguments(arguments);
    operationTimer_->setInterval(operationTimeoutMs);
    launchTimer_->start(launchTimeoutMs);
    process_->start();
}

void FirmwareWorker::cancel(const quint64 generation) {
    if (!operationActive_ || process_ == nullptr || generation != generation_) return;
    cancelRequested_ = true;
    if (launchTimer_ != nullptr) launchTimer_->stop();
    if (operationTimer_ != nullptr) operationTimer_->stop();
    if (process_->state() == QProcess::NotRunning) {
        const quint64 cancelledGeneration = generation_;
        operationActive_ = false;
        cancelRequested_ = false;
        clearOperation();
        emit operationCancelled(cancelledGeneration);
        return;
    }
    process_->kill();
}

void FirmwareWorker::finishProcess(const int exitCode, const bool normalExit) {
    if (!operationActive_ || shuttingDown_) return;
    const quint64 completedGeneration = generation_;
    if (launchTimer_ != nullptr) launchTimer_->stop();
    if (operationTimer_ != nullptr) operationTimer_->stop();

    // Drain the terminal buffers before publishing the result. espflash can
    // write the chip revision or the final progress token immediately before
    // exit, and the facade must see that output before it interprets success.
    if (process_ != nullptr) {
        const QString out = QString::fromUtf8(process_->readAllStandardOutput());
        const QString err = QString::fromUtf8(process_->readAllStandardError());
        if (!out.isEmpty()) emit outputReady(completedGeneration, out);
        if (!err.isEmpty()) emit outputReady(completedGeneration, err);
    }

    operationActive_ = false;
    const KillCause retiredCause = killCause_;
    const bool wasCancelled = cancelRequested_;
    cancelRequested_ = false;
    clearOperation();
    // Timeout is published only after the OS process has *actually* exited.
    // The facade may now drop busy/COM ownership without racing a live flasher.
    if (wasCancelled) {
        emit operationCancelled(completedGeneration);
    } else if (retiredCause == KillCause::launchTimeout) {
        emit operationLaunchTimedOut(completedGeneration);
    } else if (retiredCause == KillCause::operationTimeout) {
        emit operationTimedOut(completedGeneration);
    } else {
        emit operationFinished(completedGeneration, exitCode, normalExit);
    }
}

void FirmwareWorker::clearOperation() {
    generation_ = 0;
    killCause_ = KillCause::none;
}

void FirmwareWorker::shutdown() {
    if (!initialized_ || shuttingDown_) return;
    shuttingDown_ = true;
    if (launchTimer_ != nullptr) launchTimer_->stop();
    if (operationTimer_ != nullptr) operationTimer_->stop();
    operationActive_ = false;
    cancelRequested_ = false;
    clearOperation();

    if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
        QObject::disconnect(process_, nullptr, this, nullptr);
        process_->kill();
        static_cast<void>(process_->waitForFinished(kShutdownWaitMs));
    }
    if (process_ != nullptr) process_->close();
}
