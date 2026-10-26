// SPDX-License-Identifier: GPL-3.0-or-later
#include "M4StreamScenarioModel.hpp"
#include <QSet>
#include <QString>
#include <utility>

QVariantList M4StreamScenarioModel::roster() const {
    QVariantList out;
    if (active_) for (const auto& slot : slots_) out.push_back(slot.identity);
    return out;
}
bool M4StreamScenarioModel::openThree(const QVariantList& roster, const QVariantMap& initial) {
    if (active_ || roster.size() != 3 || initial.isEmpty()) return false;
    QSet<QString> ieds, references, wireIds;
    std::array<Slot, 3> staged{};
    for (int index = 0; index < 3; ++index) {
        const auto identity = roster[index].toMap();
        const auto ied = identity.value(QStringLiteral("ied")).toString().trimmed();
        const auto cb = identity.value(QStringLiteral("controlBlockReference")).toString().trimmed();
        const auto sv = identity.value(QStringLiteral("svId")).toString().trimmed();
        const auto mac = identity.value(QStringLiteral("destinationMac")).toString().trimmed();
        if (ied.isEmpty() || cb.isEmpty() || sv.isEmpty() || mac.isEmpty() ||
            ieds.contains(ied) || references.contains(cb) ||
            wireIds.contains(mac + QLatin1Char('/') + sv)) return false;
        ieds.insert(ied); references.insert(cb); wireIds.insert(mac + QLatin1Char('/') + sv);
        staged[static_cast<std::size_t>(index)] = {identity, initial};
    }
    slots_ = std::move(staged);
    selectedSlot_ = 0; active_ = true; emit changed();
    return true;
}
bool M4StreamScenarioModel::saveCurrent(const QVariantMap& draft) {
    if (!active_ || selectedSlot_ < 0 || draft.isEmpty()) return false;
    slots_[static_cast<std::size_t>(selectedSlot_)].draft = draft;
    return true;
}
QVariantMap M4StreamScenarioModel::select(int slot, const QVariantMap& outgoing) {
    if (!active_ || slot < 0 || slot >= 3 || outgoing.isEmpty()) return {};
    slots_[static_cast<std::size_t>(selectedSlot_)].draft = outgoing;
    if (slot != selectedSlot_) { selectedSlot_ = slot; emit changed(); }
    return slots_[static_cast<std::size_t>(slot)].draft;
}
QVariantMap M4StreamScenarioModel::draftFor(int slot) const {
    return active_ && slot >= 0 && slot < 3
        ? slots_[static_cast<std::size_t>(slot)].draft : QVariantMap{};
}
void M4StreamScenarioModel::clear() {
    if (!active_) return;
    active_ = false; selectedSlot_ = -1; slots_ = {}; emit changed();
}
