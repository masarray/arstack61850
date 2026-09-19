// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QVector>

namespace ar::iedsim::runtime_guardrails {

inline constexpr qsizetype kMaxBufferedProcessBytes = 64 * 1024;
inline constexpr qsizetype kMaxProcessLineBytes = 8 * 1024;
inline constexpr int kMaxLinesPerDrain = 64;

struct DrainResult final {
    QVector<QByteArray> lines;
    qsizetype droppedBytes{};
    int truncatedLines{};
    bool moreCompleteLines{};
};

// Appends child-process bytes into a bounded framing buffer and drains at most
// kMaxLinesPerDrain complete lines. Oversized unterminated output cannot grow
// memory without bound, and oversized individual lines are truncated before
// conversion to QString / activity events.
[[nodiscard]] DrainResult appendAndDrain(QByteArray& buffer, const QByteArray& bytes);

// Flushes an unterminated tail at process exit while preserving the same line
// cap used by normal drains.
[[nodiscard]] QByteArray boundedTail(const QByteArray& buffer, bool* truncated = nullptr);

} // namespace ar::iedsim::runtime_guardrails
