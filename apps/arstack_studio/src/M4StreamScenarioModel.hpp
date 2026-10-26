// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <array>

// Presentation-only independent MU drafts, never the firmware source of truth.
// An explicitly opened SCD roster owns 3 IED-qualified immutable identities.
class M4StreamScenarioModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int selectedSlot READ selectedSlot NOTIFY changed)
    Q_PROPERTY(QVariantList roster READ roster NOTIFY changed)
public:
    explicit M4StreamScenarioModel(QObject* parent = nullptr) : QObject(parent) {}
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] int selectedSlot() const noexcept { return selectedSlot_; }
    [[nodiscard]] QVariantList roster() const;
    Q_INVOKABLE bool openThree(const QVariantList& roster, const QVariantMap& initial);
    Q_INVOKABLE bool saveCurrent(const QVariantMap& draft);
    Q_INVOKABLE QVariantMap select(int slot, const QVariantMap& outgoing);
    Q_INVOKABLE QVariantMap draftFor(int slot) const;
    Q_INVOKABLE void clear();
signals:
    void changed();
private:
    struct Slot { QVariantMap identity; QVariantMap draft; };
    std::array<Slot, 3> slots_{};
    int selectedSlot_{-1};
    bool active_{false};
};
