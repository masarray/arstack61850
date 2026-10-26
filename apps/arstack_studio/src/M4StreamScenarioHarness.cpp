// SPDX-License-Identifier: GPL-3.0-or-later
#include "M4StreamScenarioModel.hpp"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QVariantList roster;
    for (int i = 1; i <= 3; ++i) roster.push_back(QVariantMap{
        {QStringLiteral("ied"), QStringLiteral("MU01_4I_4V_%1").arg(i)},
        {QStringLiteral("controlBlockReference"), QStringLiteral("MU%1/LLN0.MSVCB01").arg(i)},
        {QStringLiteral("svId"), QStringLiteral("MU01_SV%1").arg(i)},
        {QStringLiteral("destinationMac"), QStringLiteral("01:0C:CD:04:00:0%1").arg(i)}});
    QVariantMap initial{{QStringLiteral("hz"), 50.0}};
    M4StreamScenarioModel model;
    const auto check = [](bool value, const char* reason) {
        if (!value) qCritical() << "FAIL M4 pages" << reason;
        return value;
    };
    if (!check(model.openThree(roster, initial), "valid SCD roster") ||
        !check(model.active() && model.roster().size() == 3, "distinct roster identity")) return 1;
    auto one = initial, two = initial, three = initial;
    two.insert(QStringLiteral("hz"), 60.0);
    three.insert(QStringLiteral("hz"), 0.0);
    if (!check(model.select(1, one).value(QStringLiteral("hz")).toDouble() == 50.0, "MU2 seeded") ||
        !check(model.select(2, two).value(QStringLiteral("hz")).toDouble() == 50.0, "MU3 seeded") ||
        !check(model.select(0, three).value(QStringLiteral("hz")).toDouble() == 50.0, "MU1 retained") ||
        !check(model.draftFor(1).value(QStringLiteral("hz")).toDouble() == 60.0, "MU2 isolated") ||
        !check(model.draftFor(2).value(QStringLiteral("hz")).toDouble() == 0.0, "MU3 isolated"))
        return 1;
    model.clear();
    QVariantList duplicate = roster;
    duplicate[2] = duplicate[1];
    if (!check(!model.openThree(duplicate, initial), "reject duplicate IED") ||
        !check(!model.active(), "bad roster doesn't activate") ||
        !check(model.openThree(roster, initial), "restart after rejection") ||
        !check(model.select(3, initial).isEmpty(), "reject fourth MU") ||
        !check(model.selectedSlot() == 0, "no invalid selection"))
        return 1;
    qInfo() << "PASS M4 P0E dedicated MU drafts and IED-qualified page selection";
    return 0;
}
