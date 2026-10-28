// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/m4_three_sv_binary.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>

// Host-only, bounded and deliberately transport-agnostic.
// One command is emitted ONLY after validating the matching firmware ACK.
// The existing DeviceIoWorker remains the only serial authority when wired in.
class M4MultiSvUploadTransaction final {
public:
    enum class Stage {
        idle, status, abort_stale, begin, chunk, validate, commit,
        readback, select, verified, failed
    };

    [[nodiscard]] bool start(const QByteArray& arm3, const QString& bootId);
    [[nodiscard]] QString takeCommand();
    [[nodiscard]] bool receive(const QString& rawLine, const QString& currentBootId);
    void cancel(const QString& reason);
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] bool active() const noexcept {
        return stage_ != Stage::idle && stage_ != Stage::verified &&
               stage_ != Stage::failed;
    }
    [[nodiscard]] bool verified() const noexcept { return stage_ == Stage::verified; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString identityHash() const;
    // Read-only reconnect proof: same ARM3 decoder and per-stream validator as
    // the ACK-gated deploy; never trust a matching hash alone.
    [[nodiscard]] static bool matchesBoardRoster(
        const QByteArray& expectedArm3, const QString& boardHash,
        const QStringList& firmwareStreamLines);
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::uint32_t transaction() const noexcept { return transaction_; }

private:
    using Profiles = std::array<
        ar::iec61850::sampled_values::CompiledSvDeviceProfile,
        ar::iec61850::sampled_values::m4_three_sv_capacity>;

    void fail(const QString& reason);
    void issueBegin(std::uint32_t lastTransaction);
    void issueChunkOrValidate();
    [[nodiscard]] bool validateStream(const QString& line);
    [[nodiscard]] static QString value(const QString& line, const char* key);
    [[nodiscard]] static bool uintField(const QString& line, const char* key,
                                         std::uint64_t& output, int base = 10);
    [[nodiscard]] static QString mac(const std::array<std::uint8_t, 6>& bytes);

    QByteArray record_;
    Profiles profiles_{};
    QString bootId_;
    QString pendingCommand_;
    QString error_;
    Stage stage_{Stage::idle};
    std::uint64_t identityHash_{};
    std::uint64_t generation_{};
    std::uint32_t transaction_{};
    std::uint32_t observedLast_{};
    qsizetype offset_{};
    qsizetype expectedReceived_{};
    std::uint8_t rowsVerified_{};
    bool statusVerified_{};
};
