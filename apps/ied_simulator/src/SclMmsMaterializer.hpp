// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <vector>

namespace arstack::iedsim {

struct MaterializedSclValue final {
    QString iedName;
    QString logicalDevice;
    QString logicalNode;
    QString dataObject;
    QString dataAttribute;
    QString functionalConstraint;
    QString cdc;
    QString rawType;
    QString normalizedType;
    QString initialValue;
    QString mmsDomain;
    QString mmsItem;
    QString reference;
    bool quality{};
    bool timestamp{};
    bool mmsWritable{};
};

struct SclMmsMaterialization final {
    std::vector<MaterializedSclValue> values;
    int logicalNodeCount{};
};

namespace detail {

struct DaDecl final {
    QString name;
    QString bType;
    QString type;
    QString fc;
};

struct DoDecl final {
    QString name;
    QString type;
};

struct SdoDecl final {
    QString name;
    QString type;
};

struct BdaDecl final {
    QString name;
    QString bType;
    QString type;
};

struct LnTypeDecl final {
    std::vector<DoDecl> dataObjects;
};

struct DoTypeDecl final {
    QString cdc;
    std::vector<DaDecl> dataAttributes;
    std::vector<SdoDecl> subDataObjects;
};

struct DaTypeDecl final {
    std::vector<BdaDecl> basicDataAttributes;
};

struct LnInstance final {
    QString iedName;
    QString ldInst;
    QString prefix;
    QString lnClass;
    QString lnInst;
    QString lnType;
};

inline QString normalizedType(const QString& rawType, const QString& cdc, const QString& attribute) {
    if (rawType.compare(QStringLiteral("Quality"), Qt::CaseInsensitive) == 0 ||
        attribute.endsWith(QStringLiteral(".q"), Qt::CaseInsensitive) ||
        attribute.compare(QStringLiteral("q"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Quality");
    }
    if (rawType.compare(QStringLiteral("Timestamp"), Qt::CaseInsensitive) == 0 ||
        attribute.endsWith(QStringLiteral(".t"), Qt::CaseInsensitive) ||
        attribute.compare(QStringLiteral("t"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Timestamp");
    }
    if (rawType.contains(QStringLiteral("Enum"), Qt::CaseInsensitive) ||
        cdc.compare(QStringLiteral("DPC"), Qt::CaseInsensitive) == 0 &&
            attribute.endsWith(QStringLiteral("stVal"), Qt::CaseInsensitive)) {
        return QStringLiteral("Enumeration");
    }
    if (rawType.compare(QStringLiteral("BOOLEAN"), Qt::CaseInsensitive) == 0 ||
        rawType.contains(QStringLiteral("Bool"), Qt::CaseInsensitive)) {
        return QStringLiteral("Boolean");
    }
    if (rawType.contains(QStringLiteral("Float"), Qt::CaseInsensitive) ||
        rawType.contains(QStringLiteral("INT"), Qt::CaseInsensitive) ||
        rawType.contains(QStringLiteral("Integer"), Qt::CaseInsensitive)) {
        return QStringLiteral("Number");
    }
    return rawType.isEmpty() ? QStringLiteral("Text") : rawType;
}

inline QString initialValue(const QString& normalized, const QString& attribute) {
    if (normalized == QStringLiteral("Boolean")) return QStringLiteral("false");
    if (normalized == QStringLiteral("Enumeration")) return QStringLiteral("intermediate-state");
    if (normalized == QStringLiteral("Quality")) return QStringLiteral("Good");
    if (normalized == QStringLiteral("Timestamp")) return QStringLiteral("1970-01-01 00:00:00.000");
    if (normalized == QStringLiteral("Number")) return QStringLiteral("0");
    if (attribute.endsWith(QStringLiteral("stVal"), Qt::CaseInsensitive)) return QStringLiteral("off");
    return QStringLiteral("—");
}

inline bool mmsWritableFc(const QString& fc) {
    return fc.compare(QStringLiteral("SP"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("SG"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("SE"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("SV"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("CF"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("DC"), Qt::CaseInsensitive) == 0;
}

inline QString mmsPath(QString value) {
    value.replace(QLatin1Char('.'), QLatin1Char('$'));
    return value;
}

} // namespace detail

inline SclMmsMaterialization materializeSclMmsModel(const QString& path) {
    using namespace detail;

    QFile input{path};
    if (!input.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Could not open SCL source for full MMS materialization.");
    }

    QHash<QString, LnTypeDecl> lnTypes;
    QHash<QString, DoTypeDecl> doTypes;
    QHash<QString, DaTypeDecl> daTypes;
    std::vector<LnInstance> instances;

    QString currentIed;
    QString currentLd;
    QString currentLnType;
    QString currentDoType;
    QString currentDaType;

    QXmlStreamReader xml{&input};
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            const auto a = xml.attributes();
            if (name == QStringLiteral("IED")) {
                currentIed = a.value(QStringLiteral("name")).toString();
            } else if (name == QStringLiteral("LDevice")) {
                currentLd = a.value(QStringLiteral("inst")).toString();
            } else if ((name == QStringLiteral("LN") || name == QStringLiteral("LN0")) &&
                       !currentIed.isEmpty() && !currentLd.isEmpty()) {
                instances.push_back(LnInstance{
                    currentIed,
                    currentLd,
                    a.value(QStringLiteral("prefix")).toString(),
                    a.value(QStringLiteral("lnClass")).toString(),
                    a.value(QStringLiteral("inst")).toString(),
                    a.value(QStringLiteral("lnType")).toString()});
            } else if (name == QStringLiteral("LNodeType")) {
                currentLnType = a.value(QStringLiteral("id")).toString();
                if (!currentLnType.isEmpty()) lnTypes[currentLnType];
            } else if (name == QStringLiteral("DO") && !currentLnType.isEmpty()) {
                lnTypes[currentLnType].dataObjects.push_back(DoDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("type")).toString()});
            } else if (name == QStringLiteral("DOType")) {
                currentDoType = a.value(QStringLiteral("id")).toString();
                if (!currentDoType.isEmpty()) {
                    auto& decl = doTypes[currentDoType];
                    decl.cdc = a.value(QStringLiteral("cdc")).toString();
                }
            } else if (name == QStringLiteral("DA") && !currentDoType.isEmpty()) {
                doTypes[currentDoType].dataAttributes.push_back(DaDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("bType")).toString(),
                    a.value(QStringLiteral("type")).toString(),
                    a.value(QStringLiteral("fc")).toString()});
            } else if (name == QStringLiteral("SDO") && !currentDoType.isEmpty()) {
                doTypes[currentDoType].subDataObjects.push_back(SdoDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("type")).toString()});
            } else if (name == QStringLiteral("DAType")) {
                currentDaType = a.value(QStringLiteral("id")).toString();
                if (!currentDaType.isEmpty()) daTypes[currentDaType];
            } else if (name == QStringLiteral("BDA") && !currentDaType.isEmpty()) {
                daTypes[currentDaType].basicDataAttributes.push_back(BdaDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("bType")).toString(),
                    a.value(QStringLiteral("type")).toString()});
            }
        } else if (xml.isEndElement()) {
            const auto name = xml.name();
            if (name == QStringLiteral("LNodeType")) currentLnType.clear();
            else if (name == QStringLiteral("DOType")) currentDoType.clear();
            else if (name == QStringLiteral("DAType")) currentDaType.clear();
            else if (name == QStringLiteral("LDevice")) currentLd.clear();
            else if (name == QStringLiteral("IED")) currentIed.clear();
        }
    }
    if (xml.hasError()) {
        throw std::runtime_error(xml.errorString().toStdString());
    }

    SclMmsMaterialization result;
    result.logicalNodeCount = static_cast<int>(instances.size());

    const auto addLeaf = [&](const LnInstance& instance,
                             const QString& doPath,
                             const QString& daPath,
                             const QString& fc,
                             const QString& cdc,
                             const QString& rawType) {
        if (fc.isEmpty() || doPath.isEmpty() || daPath.isEmpty()) return;
        const auto ln = instance.prefix + instance.lnClass + instance.lnInst;
        if (ln.isEmpty()) return;
        const auto normalized = normalizedType(rawType, cdc, daPath);
        MaterializedSclValue value;
        value.iedName = instance.iedName;
        value.logicalDevice = instance.ldInst;
        value.logicalNode = ln;
        value.dataObject = doPath;
        value.dataAttribute = daPath;
        value.functionalConstraint = fc;
        value.cdc = cdc;
        value.rawType = rawType;
        value.normalizedType = normalized;
        value.initialValue = initialValue(normalized, daPath);
        value.mmsDomain = instance.iedName + instance.ldInst;
        value.mmsItem = ln + QLatin1Char('$') + fc + QLatin1Char('$') +
            mmsPath(doPath) + QLatin1Char('$') + mmsPath(daPath);
        value.reference = value.mmsDomain + QLatin1Char('/') + ln + QLatin1Char('.') +
            doPath + QLatin1Char('.') + daPath;
        value.quality = normalized == QStringLiteral("Quality");
        value.timestamp = normalized == QStringLiteral("Timestamp");
        value.mmsWritable = mmsWritableFc(fc) && !value.quality && !value.timestamp;
        result.values.push_back(std::move(value));
    };

    std::function<void(const LnInstance&, const QString&, const QString&, const QString&, const QString&, int)> expandDaType;
    expandDaType = [&](const LnInstance& instance,
                       const QString& doPath,
                       const QString& daPrefix,
                       const QString& fc,
                       const QString& daType,
                       const int depth) {
        if (depth > 16) return;
        const auto it = daTypes.constFind(daType);
        if (it == daTypes.cend()) return;
        for (const auto& bda : it->basicDataAttributes) {
            const auto pathHere = daPrefix.isEmpty() ? bda.name : daPrefix + QLatin1Char('.') + bda.name;
            if (bda.bType.compare(QStringLiteral("Struct"), Qt::CaseInsensitive) == 0 && !bda.type.isEmpty()) {
                expandDaType(instance, doPath, pathHere, fc, bda.type, depth + 1);
            } else {
                addLeaf(instance, doPath, pathHere, fc, QString{}, bda.bType);
            }
        }
    };

    std::function<void(const LnInstance&, const QString&, const QString&, int)> expandDoType;
    expandDoType = [&](const LnInstance& instance,
                       const QString& doPath,
                       const QString& doType,
                       const int depth) {
        if (depth > 16) return;
        const auto it = doTypes.constFind(doType);
        if (it == doTypes.cend()) return;
        const auto cdc = it->cdc;
        for (const auto& da : it->dataAttributes) {
            if (da.bType.compare(QStringLiteral("Struct"), Qt::CaseInsensitive) == 0 && !da.type.isEmpty()) {
                const auto before = result.values.size();
                expandDaType(instance, doPath, da.name, da.fc, da.type, depth + 1);
                for (auto index = before; index < result.values.size(); ++index) {
                    if (result.values[index].cdc.isEmpty()) result.values[index].cdc = cdc;
                }
            } else {
                addLeaf(instance, doPath, da.name, da.fc, cdc, da.bType);
            }
        }
        for (const auto& sdo : it->subDataObjects) {
            const auto nestedPath = doPath.isEmpty() ? sdo.name : doPath + QLatin1Char('.') + sdo.name;
            expandDoType(instance, nestedPath, sdo.type, depth + 1);
        }
    };

    for (const auto& instance : instances) {
        const auto lnIt = lnTypes.constFind(instance.lnType);
        if (lnIt == lnTypes.cend()) continue;
        for (const auto& dataObject : lnIt->dataObjects) {
            expandDoType(instance, dataObject.name, dataObject.type, 0);
        }
    }

    std::sort(result.values.begin(), result.values.end(), [](const auto& left, const auto& right) {
        if (left.mmsDomain != right.mmsDomain) return left.mmsDomain < right.mmsDomain;
        return left.mmsItem < right.mmsItem;
    });
    result.values.erase(
        std::unique(result.values.begin(), result.values.end(), [](const auto& left, const auto& right) {
            return left.mmsDomain == right.mmsDomain && left.mmsItem == right.mmsItem;
        }),
        result.values.end());
    return result;
}

} // namespace arstack::iedsim
