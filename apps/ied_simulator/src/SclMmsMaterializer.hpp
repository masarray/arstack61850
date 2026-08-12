// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QHash>
#include <QSet>
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
    QString mmsTypeSignature;
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
    int count{1};
};

struct DoDecl final {
    QString name;
    QString type;
    int count{1};
};

struct SdoDecl final {
    QString name;
    QString type;
    int count{1};
};

struct BdaDecl final {
    QString name;
    QString bType;
    QString type;
    int count{1};
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

inline int elementCount(const QString& text) {
    bool ok = false;
    const auto parsed = text.toInt(&ok);
    return ok && parsed > 0 ? parsed : 1;
}

inline QString scalarMmsTypeSignature(const QString& rawType) {
    const auto raw = rawType.trimmed().toUpper();
    if (raw == QStringLiteral("BOOLEAN")) return QStringLiteral("boolean");
    if (raw == QStringLiteral("INT8")) return QStringLiteral("integer:8");
    if (raw == QStringLiteral("INT16")) return QStringLiteral("integer:16");
    if (raw == QStringLiteral("INT24")) return QStringLiteral("integer:24");
    if (raw == QStringLiteral("INT32")) return QStringLiteral("integer:32");
    if (raw == QStringLiteral("INT64")) return QStringLiteral("integer:64");
    if (raw == QStringLiteral("INT128")) return QStringLiteral("integer:128");
    if (raw == QStringLiteral("INT8U")) return QStringLiteral("unsigned-integer:8");
    if (raw == QStringLiteral("INT16U")) return QStringLiteral("unsigned-integer:16");
    if (raw == QStringLiteral("INT24U")) return QStringLiteral("unsigned-integer:24");
    if (raw == QStringLiteral("INT32U")) return QStringLiteral("unsigned-integer:32");
    if (raw == QStringLiteral("INT64U")) return QStringLiteral("unsigned-integer:64");
    if (raw == QStringLiteral("FLOAT32")) return QStringLiteral("floating-point:32:8");
    if (raw == QStringLiteral("FLOAT64")) return QStringLiteral("floating-point:64:11");
    if (raw == QStringLiteral("ENUM")) return QStringLiteral("integer:32");
    if (raw == QStringLiteral("DBPOS")) return QStringLiteral("bit-string:2");
    if (raw == QStringLiteral("QUALITY")) return QStringLiteral("bit-string:13");
    if (raw == QStringLiteral("TIMESTAMP")) return QStringLiteral("utc-time");
    if (raw == QStringLiteral("VISSTRING32")) return QStringLiteral("visible-string:32");
    if (raw == QStringLiteral("VISSTRING64")) return QStringLiteral("visible-string:64");
    if (raw == QStringLiteral("VISSTRING65")) return QStringLiteral("visible-string:65");
    if (raw == QStringLiteral("VISSTRING129")) return QStringLiteral("visible-string:129");
    if (raw == QStringLiteral("VISSTRING255")) return QStringLiteral("visible-string:255");
    if (raw == QStringLiteral("OBJREF")) return QStringLiteral("visible-string:129");
    if (raw == QStringLiteral("UNICODE255")) return QStringLiteral("mms-string:255");
    if (raw == QStringLiteral("OCTET64")) return QStringLiteral("octet-string:64");
    if (raw == QStringLiteral("ENTRYID")) return QStringLiteral("octet-string:8");
    if (raw == QStringLiteral("CHECK")) return QStringLiteral("bit-string:2");
    if (raw == QStringLiteral("TCMD")) return QStringLiteral("integer:32");
    if (raw == QStringLiteral("TRGOPS")) return QStringLiteral("bit-string:6");
    if (raw == QStringLiteral("OPTFLDS")) return QStringLiteral("bit-string:10");
    if (raw == QStringLiteral("CURRENCY")) return QStringLiteral("visible-string:3");
    return {};
}

} // namespace detail

inline QString sclMmsTypeSignature(const QString& rawType, const int count = 1) {
    const auto scalar = detail::scalarMmsTypeSignature(rawType);
    if (scalar.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("Unsupported SCL bType for exact MMS projection: %1")
                .arg(rawType).toStdString());
    }
    return count > 1
        ? QStringLiteral("array:%1:%2").arg(count).arg(scalar)
        : scalar;
}

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
                    a.value(QStringLiteral("type")).toString(),
                    elementCount(a.value(QStringLiteral("count")).toString())});
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
                    a.value(QStringLiteral("fc")).toString(),
                    elementCount(a.value(QStringLiteral("count")).toString())});
            } else if (name == QStringLiteral("SDO") && !currentDoType.isEmpty()) {
                doTypes[currentDoType].subDataObjects.push_back(SdoDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("type")).toString(),
                    elementCount(a.value(QStringLiteral("count")).toString())});
            } else if (name == QStringLiteral("DAType")) {
                currentDaType = a.value(QStringLiteral("id")).toString();
                if (!currentDaType.isEmpty()) daTypes[currentDaType];
            } else if (name == QStringLiteral("BDA") && !currentDaType.isEmpty()) {
                daTypes[currentDaType].basicDataAttributes.push_back(BdaDecl{
                    a.value(QStringLiteral("name")).toString(),
                    a.value(QStringLiteral("bType")).toString(),
                    a.value(QStringLiteral("type")).toString(),
                    elementCount(a.value(QStringLiteral("count")).toString())});
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
                             const QString& rawType,
                             const int count) {
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
        value.mmsTypeSignature = sclMmsTypeSignature(rawType, count);
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
                if (bda.count != 1) {
                    throw std::runtime_error("Arrays of structured BDA values are not supported by the bounded host model.");
                }
                expandDaType(instance, doPath, pathHere, fc, bda.type, depth + 1);
            } else {
                addLeaf(instance, doPath, pathHere, fc, QString{}, bda.bType, bda.count);
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
                if (da.count != 1) {
                    throw std::runtime_error("Arrays of structured DA values are not supported by the bounded host model.");
                }
                const auto before = result.values.size();
                expandDaType(instance, doPath, da.name, da.fc, da.type, depth + 1);
                for (auto index = before; index < result.values.size(); ++index) {
                    if (result.values[index].cdc.isEmpty()) result.values[index].cdc = cdc;
                }
            } else {
                addLeaf(instance, doPath, da.name, da.fc, cdc, da.bType, da.count);
            }
        }
        for (const auto& sdo : it->subDataObjects) {
            if (sdo.count != 1) {
                throw std::runtime_error("Arrays of SDO values are not supported by the bounded host model.");
            }
            const auto nestedPath = doPath.isEmpty() ? sdo.name : doPath + QLatin1Char('.') + sdo.name;
            expandDoType(instance, nestedPath, sdo.type, depth + 1);
        }
    };

    for (const auto& instance : instances) {
        const auto lnIt = lnTypes.constFind(instance.lnType);
        if (lnIt == lnTypes.cend()) continue;
        for (const auto& dataObject : lnIt->dataObjects) {
            if (dataObject.count != 1) {
                throw std::runtime_error("Arrays of DO values are not supported by the bounded host model.");
            }
            expandDoType(instance, dataObject.name, dataObject.type, 0);
        }
    }

    // Declaration order is part of the structured MMS TypeSpecification.
    // Keep the SCL expansion order and only remove duplicate canonical leaves.
    QSet<QString> seen;
    std::vector<MaterializedSclValue> stable;
    stable.reserve(result.values.size());
    for (auto& value : result.values) {
        const auto key = value.mmsDomain + QLatin1Char('\n') + value.mmsItem;
        if (seen.contains(key)) continue;
        seen.insert(key);
        stable.push_back(std::move(value));
    }
    result.values = std::move(stable);
    return result;
}

} // namespace arstack::iedsim
