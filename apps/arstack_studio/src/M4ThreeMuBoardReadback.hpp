// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ariec61850/sampled_values/live_signal_state.hpp"
#include <QString>
#include <QVariantList>
#include <array>
#include <cstdint>

// Read-only complete 3-MU board state. One request and one complete,
// hash-verified atomic firmware bank snapshot per slot. No writes, no START,
// no live draft mutation. Caller binds each request to transport/boot.
class M4ThreeMuBoardReadback final {
public:
    enum class Stage { idle, send, waiting_begin, waiting_rows, verified, failed };
    bool start(const QString& bootId);
    QString takeCommand();
    bool receive(const QString& raw, const QString& currentBoot);
    void cancel(const QString& why);
    [[nodiscard]] bool active() const noexcept {
        return stage_ == Stage::send || stage_ == Stage::waiting_begin ||
               stage_ == Stage::waiting_rows;
    }
    [[nodiscard]] bool verified() const noexcept { return stage_ == Stage::verified; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] int verifiedSlots() const noexcept { return slot_; }
    [[nodiscard]] QVariantList snapshots() const;
private:
    struct Slot {
        ar::iec61850::sampled_values::SvLiveSignalState state{};
        std::uint64_t hash{0};
        bool txEnabled{false};
    };
    void fail(const QString& message);
    [[nodiscard]] bool parseBegin(const QString& line);
    [[nodiscard]] bool parseRow(const QString& line);
    [[nodiscard]] bool parseEnd(const QString& line);
    static QString field(const QString& line, const char* key);
    std::array<Slot,3> verified_{};
    Slot staging_{};
    QString boot_;
    QString error_;
    Stage stage_{Stage::idle};
    int slot_{0};
    unsigned rows_{0};
};
