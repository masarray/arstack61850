// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/publisher_profile.hpp"
#include "ariec61850/scl/model.hpp"

#include <QAbstractListModel>
#include <QByteArray>
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
    Q_PROPERTY(bool referenceTemplateActive READ referenceTemplateActive NOTIFY sourceChanged)
    Q_PROPERTY(bool m4DraftSupported READ m4DraftSupported NOTIFY sourceChanged)
    Q_PROPERTY(QString m4ConfigurationHash READ m4ConfigurationHash NOTIFY sourceChanged)

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
    [[nodiscard]] bool referenceTemplateActive() const noexcept;
    [[nodiscard]] bool m4DraftSupported() const noexcept;
    Q_INVOKABLE QVariantList m4RosterRows() const;
    // Canonical, immutable after SCD recompilation; never encode per GUI frame.
    // Empty on any invalid MU or duplicate wire identity.
    Q_INVOKABLE QByteArray m4ConfigurationBinary() const;
    [[nodiscard]] QString m4ConfigurationHash() const;

    Q_INVOKABLE bool loadFile(const QUrl& fileUrl);
    Q_INVOKABLE bool loadReferenceTemplate();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectStream(int row);
    Q_INVOKABLE bool confirmCounterModulus(int modulus);
    Q_INVOKABLE void clearCounterConfirmation();
    Q_INVOKABLE bool confirmNominalFrequencyHz(int frequencyHz);
    Q_INVOKABLE void clearNominalFrequency();
    Q_INVOKABLE bool selectProfileFamily(const QString& familyKey);
    // Operator L2 override is compiled into the SAME canonical device profile
    // and binary transaction. It is never a display-only VLAN setting.
    Q_INVOKABLE bool configureSelectedVlan(bool present, int vid, int pcp);

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

    void installDocument(ar::iec61850::scl::SclDocument document, bool referenceTemplate);
    struct VlanOverride final {
        bool present{};
        std::uint16_t vid{};
        std::uint8_t pcp{};
    };
    void rebuildRows();
    [[nodiscard]] QVariantMap profileToVariantMap(
        const ar::iec61850::sampled_values::SvPublisherProfile& profile) const;

    std::optional<ar::iec61850::scl::SclDocument> document_;
    std::optional<std::uint16_t> confirmedCounterModulus_;
    std::optional<std::uint32_t> confirmedNominalFrequencyMilliHz_;
    std::vector<ar::iec61850::sampled_values::SvProfileFamily> profileFamilies_;
    std::vector<std::optional<VlanOverride>> vlanOverrides_;
    std::vector<Row> rows_;
    QByteArray m4BinaryEnvelope_;
    QString m4EnvelopeHash_;
    int selectedIndex_{-1};
    QString fatalError_;
    bool referenceTemplateActive_{false};
};
