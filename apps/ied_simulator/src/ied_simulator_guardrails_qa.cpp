// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedActivityModel.hpp"
#include "IedRuntimeGuardrails.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <iostream>

namespace {
void spin(const int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

int fail(const char* message) {
    std::cerr << "IEDSIM_GUARDRAILS_FAIL " << message << '\n';
    return 1;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QByteArray unterminated;
    const QByteArray flood(2 * 1024 * 1024, 'x');
    const auto floodResult = ar::iedsim::runtime_guardrails::appendAndDrain(unterminated, flood);
    if (unterminated.size() > ar::iedsim::runtime_guardrails::kMaxBufferedProcessBytes) {
        return fail("unterminated child output exceeded hard buffer cap");
    }
    if (floodResult.droppedBytes <= 0) return fail("flood did not report discarded bytes");

    QByteArray burst;
    QByteArray manyLines;
    for (int index = 0; index < 200; ++index) {
        manyLines += "IEDSIM_EVENT kind=test index=" + QByteArray::number(index) + '\n';
    }
    const auto firstDrain = ar::iedsim::runtime_guardrails::appendAndDrain(burst, manyLines);
    if (firstDrain.lines.size() != ar::iedsim::runtime_guardrails::kMaxLinesPerDrain) {
        return fail("per-turn line drain budget is not enforced");
    }
    if (!firstDrain.moreCompleteLines) return fail("remaining complete lines were not retained");

    int drained = firstDrain.lines.size();
    while (burst.indexOf('\n') >= 0) {
        drained += ar::iedsim::runtime_guardrails::appendAndDrain(burst, {}).lines.size();
    }
    if (drained != 200) return fail("line framing lost events within the bounded buffer");

    IedActivityModel activity;
    for (int index = 0; index < 5'000; ++index) {
        QVariantMap event;
        event.insert(QStringLiteral("time"), QString::number(index));
        event.insert(QStringLiteral("category"), QStringLiteral("Burst"));
        event.insert(QStringLiteral("message"), QStringLiteral("event-%1").arg(index));
        event.insert(
            QStringLiteral("severity"),
            index % 10 == 0 ? QStringLiteral("Error") : QStringLiteral("Info"));
        event.insert(QStringLiteral("ied"), QStringLiteral("IED-A"));
        activity.push_front(event);
    }
    spin(30);
    if (activity.retainedCount() > 300) return fail("activity retention exceeded 300 events");
    if (activity.rowCount() != activity.visibleCount()) return fail("visible activity count is inconsistent");

    activity.setSeverityFilter(QStringLiteral("Error"));
    spin(100);
    for (int row = 0; row < activity.rowCount(); ++row) {
        if (activity.data(activity.index(row, 0), IedActivityModel::SeverityRole).toString() !=
            QStringLiteral("Error")) {
            return fail("severity filter leaked a non-error row");
        }
    }

    activity.setSeverityFilter(QStringLiteral("All"));
    activity.setFilterText(QStringLiteral("event-4999"));
    spin(100);
    if (activity.rowCount() != 1) return fail("text filtering did not isolate the expected newest event");

    std::cout
        << "IEDSIM_GUARDRAILS_PASS"
        << " output_cap=" << ar::iedsim::runtime_guardrails::kMaxBufferedProcessBytes
        << " drain_budget=" << ar::iedsim::runtime_guardrails::kMaxLinesPerDrain
        << " retained=" << activity.retainedCount()
        << " flood_dropped=" << floodResult.droppedBytes
        << '\n';
    return 0;
}
