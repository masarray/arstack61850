// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <cstdint>

// Fixed-capacity host control-plane only: validates ALL three before any UART
// mutation, and requires a matching firmware COMMIT + READBACK per MU.
// The caller remains the sole serial owner and enforces session/timeout/STOP.
class M4ThreeMuScenarioApply final {
public:
    enum class Stage { idle, send_batch, wait_commit, send_readback, wait_readback, verified, failed };
    bool start(const QVariantList& scenarios, const QString& bootId);
    QStringList takeCommands();  // exclusive batch, or a single readback
    bool receive(const QString& raw, const QString& currentBootId);
    void cancel(const QString& why);
    [[nodiscard]] bool active() const noexcept { return stage_ != Stage::idle &&
        stage_ != Stage::verified && stage_ != Stage::failed; }
    [[nodiscard]] bool verified() const noexcept { return stage_ == Stage::verified; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] int slot() const noexcept { return slot_ + 1; }
    [[nodiscard]] int verifiedSlots() const noexcept { return verifiedSlots_; }
    [[nodiscard]] std::uint64_t generation(int slot) const noexcept {
        return slot >= 0 && slot < 3 ? slots_[static_cast<std::size_t>(slot)].generation : 0;
    }
private:
    struct Slot final {
        QStringList batch;
        std::uint64_t token{};
        std::uint64_t hash{};
        std::uint64_t generation{};
    };
    static bool buildSlot(const QVariantMap& draft, int slot, Slot& out, QString& error);
    static QString field(const QString& s, const char* key);
    void fail(const QString& why);
    std::array<Slot, 3> slots_{};
    QString boot_;
    QString error_;
    Stage stage_{Stage::idle};
    int slot_{0};
    int verifiedSlots_{0};
};
