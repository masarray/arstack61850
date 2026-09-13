// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedRuntimeGuardrails.hpp"

#include <algorithm>

namespace ar::iedsim::runtime_guardrails {
namespace {
QByteArray boundedLine(const QByteArray& line, int& truncatedLines) {
    if (line.size() <= kMaxProcessLineBytes) return line;
    ++truncatedLines;
    auto result = line.left(kMaxProcessLineBytes);
    result += " …[truncated]";
    return result;
}
} // namespace

DrainResult appendAndDrain(QByteArray& buffer, const QByteArray& bytes) {
    DrainResult result;

    if (!bytes.isEmpty()) {
        const qsizetype combinedSize = buffer.size() + bytes.size();
        if (combinedSize > kMaxBufferedProcessBytes) {
            const qsizetype overflow = combinedSize - kMaxBufferedProcessBytes;
            result.droppedBytes += overflow;

            if (overflow >= buffer.size()) {
                const qsizetype dropFromBytes = overflow - buffer.size();
                buffer.clear();
                buffer += bytes.mid(std::min(dropFromBytes, bytes.size()));
            } else {
                buffer.remove(0, overflow);
                buffer += bytes;
            }
        } else {
            buffer += bytes;
        }
    }

    result.lines.reserve(kMaxLinesPerDrain);
    for (int count = 0; count < kMaxLinesPerDrain; ++count) {
        const auto newline = buffer.indexOf('\n');
        if (newline < 0) break;
        auto line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (!line.isEmpty() && line.endsWith('\r')) line.chop(1);
        if (!line.trimmed().isEmpty()) {
            result.lines.push_back(boundedLine(line, result.truncatedLines));
        }
    }
    result.moreCompleteLines = buffer.indexOf('\n') >= 0;
    return result;
}

QByteArray boundedTail(const QByteArray& buffer, bool* truncated) {
    const bool didTruncate = buffer.size() > kMaxProcessLineBytes;
    if (truncated != nullptr) *truncated = didTruncate;
    if (!didTruncate) return buffer;
    auto result = buffer.left(kMaxProcessLineBytes);
    result += " …[truncated]";
    return result;
}

} // namespace ar::iedsim::runtime_guardrails
