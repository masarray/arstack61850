// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QLockFile>
#include <QStandardPaths>
#include <QString>

// Process-level ownership barrier for the desktop instrument.
//
// The lock is intentionally acquired in main() before FirmwareManager, QML, or
// DeviceIoWorker is created. This prevents a second Studio process from ever
// reaching serial-port ownership arbitration for the same injector.
class SingleInstanceGuard final {
public:
    explicit SingleInstanceGuard(QString lockPath = lockFilePath())
        : lock_(std::move(lockPath)) {}

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    [[nodiscard]] bool tryAcquire() {
        if (acquired_) return true;
        acquired_ = lock_.tryLock(0);
        return acquired_;
    }

    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

    [[nodiscard]] static QString lockFilePath() {
        QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (root.trimmed().isEmpty()) root = QDir::tempPath();
        return QDir(root).filePath(QStringLiteral("arstack-studio-single-instance.lock"));
    }

    [[nodiscard]] static constexpr int contentionExitCode() noexcept { return 23; }

private:
    QLockFile lock_;
    bool acquired_{false};
};