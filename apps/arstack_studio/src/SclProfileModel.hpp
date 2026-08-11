// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/publisher_profile.hpp"
#include "ariec61850/scl/model.hpp"

#include <QAbstractListModel>
#include <QUrl>
#include <QVariantMap>

#include <optional>
#include <string>
#include <vector>

class SclProfileModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QString documentStatus READ documentStatus NOTIFY sourceChanged)
    Q_PROPERTY(QStringList documentWarnings READ documentWarnings NOTIFY sourceChanged)
    Q_PROPERTY(QString fatalError READ fatalError NOTIFY fatalErrorChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE selectStream NOTIFY selectedIndexChanged)
    Q_PROPERTY(QVariantMap selectedProfile READ selectedProfile NOTIFY selectedProfileChanged)
    Q_PROPERTY(bool hasProfiles READ hasProfiles NOTIFY sourceChanged)

public:
    enum Roles {
        IedRole = Qt::UserRole + 1,
        ControlRole,
        ControlBlockReferenceRole,
        SvIdRole,
        CompatibilityClassRole,
        DeviceSupportRole,
        DestinationMacRole,
        AppIdRole,
        PublisherRateRole,
        PayloadBytesRole,
        CounterPolicyRole,
        CounterModulusRole,
        WarningsRole,
        ErrorsRole,
    };

    explicit SclProfileModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString sourceName() const;
    [[nodiscard]] QString documentStatus() const;
    [[nodiscard]] QStringList documentWarnings() const;
    [[nodiscard]] QString fatalError() const;
    [[nodiscard]] int selectedIndex() const noexcept;
    [[nodiscard]] QVariantMap selectedProfile() const;
    [[nodiscard]] bool hasProfiles() const noexcept;

    Q_INVOKABLE bool loadFile(const QUrl& fileUrl);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectStream(int row);
    Q_INVOKABLE bool confirmCounterModulus(int modulus);
    Q_INVOKABLE void clearCounterConfirmation();

signals:
    void sourceChanged();
    void fatalErrorChanged();
    void selectedIndexChanged();
    void selectedProfileChanged();

private:
    struct Row final {
        QString ied;
        QString control;
        QString controlBlockReference;
        QString compatibilityClass;
        QString deviceSupport;
        QStringList warnings;
        QStringList errors;
        std::optional<ar::iec61850::sampled_values::SvPublisherProfile> profile;
    };

    void rebuildRows();
    [[nodiscard]] QVariantMap profileToVariantMap(
        const ar::iec61850::sampled_values::SvPublisherProfile& profile) const;

    std::optional<ar::iec61850::scl::SclDocument> document_;
    std::optional<std::uint16_t> confirmedCounterModulus_;
    std::vector<Row> rows_;
    int selectedIndex_{-1};
    QString fatalError_;
};