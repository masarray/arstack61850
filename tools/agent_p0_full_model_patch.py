from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"needle not found in {path}: {old[:120]!r}")
    if text.count(old) != 1:
        raise SystemExit(f"needle not unique in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def replace_range(path: str, start: str, end: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"start marker not found in {path}: {start!r}")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"end marker not found in {path}: {end!r}")
    p.write_text(text[:a] + new + text[b:], encoding="utf-8")


# ---------------------------------------------------------------------------
# SCL structural materialization: preserve declaration order and carry an
# exact MMS scalar/array type signature instead of reducing types to UI labels.
# ---------------------------------------------------------------------------
path = "apps/ied_simulator/src/SclMmsMaterializer.hpp"
replace_once(path, "#include <QHash>\n", "#include <QHash>\n#include <QSet>\n")
replace_once(path,
'''    QString rawType;\n    QString normalizedType;\n    QString initialValue;''',
'''    QString rawType;\n    QString normalizedType;\n    QString mmsTypeSignature;\n    QString initialValue;''')
replace_once(path,
'''struct DaDecl final {\n    QString name;\n    QString bType;\n    QString type;\n    QString fc;\n};\n\nstruct DoDecl final {\n    QString name;\n    QString type;\n};\n\nstruct SdoDecl final {\n    QString name;\n    QString type;\n};\n\nstruct BdaDecl final {\n    QString name;\n    QString bType;\n    QString type;\n};''',
'''struct DaDecl final {\n    QString name;\n    QString bType;\n    QString type;\n    QString fc;\n    int count{1};\n};\n\nstruct DoDecl final {\n    QString name;\n    QString type;\n    int count{1};\n};\n\nstruct SdoDecl final {\n    QString name;\n    QString type;\n    int count{1};\n};\n\nstruct BdaDecl final {\n    QString name;\n    QString bType;\n    QString type;\n    int count{1};\n};''')
replace_once(path,
'''inline QString mmsPath(QString value) {\n    value.replace(QLatin1Char('.'), QLatin1Char('$'));\n    return value;\n}\n\n} // namespace detail''',
'''inline QString mmsPath(QString value) {\n    value.replace(QLatin1Char('.'), QLatin1Char('$'));\n    return value;\n}\n\ninline int elementCount(const QString& text) {\n    bool ok = false;\n    const auto parsed = text.toInt(&ok);\n    return ok && parsed > 0 ? parsed : 1;\n}\n\ninline QString scalarMmsTypeSignature(const QString& rawType) {\n    const auto raw = rawType.trimmed().toUpper();\n    if (raw == QStringLiteral("BOOLEAN")) return QStringLiteral("boolean");\n    if (raw == QStringLiteral("INT8")) return QStringLiteral("integer:8");\n    if (raw == QStringLiteral("INT16")) return QStringLiteral("integer:16");\n    if (raw == QStringLiteral("INT24")) return QStringLiteral("integer:24");\n    if (raw == QStringLiteral("INT32")) return QStringLiteral("integer:32");\n    if (raw == QStringLiteral("INT64")) return QStringLiteral("integer:64");\n    if (raw == QStringLiteral("INT128")) return QStringLiteral("integer:128");\n    if (raw == QStringLiteral("INT8U")) return QStringLiteral("unsigned-integer:8");\n    if (raw == QStringLiteral("INT16U")) return QStringLiteral("unsigned-integer:16");\n    if (raw == QStringLiteral("INT24U")) return QStringLiteral("unsigned-integer:24");\n    if (raw == QStringLiteral("INT32U")) return QStringLiteral("unsigned-integer:32");\n    if (raw == QStringLiteral("INT64U")) return QStringLiteral("unsigned-integer:64");\n    if (raw == QStringLiteral("FLOAT32")) return QStringLiteral("floating-point:32:8");\n    if (raw == QStringLiteral("FLOAT64")) return QStringLiteral("floating-point:64:11");\n    if (raw == QStringLiteral("ENUM")) return QStringLiteral("integer:32");\n    if (raw == QStringLiteral("DBPOS")) return QStringLiteral("bit-string:2");\n    if (raw == QStringLiteral("QUALITY")) return QStringLiteral("bit-string:13");\n    if (raw == QStringLiteral("TIMESTAMP")) return QStringLiteral("utc-time");\n    if (raw == QStringLiteral("VISSTRING32")) return QStringLiteral("visible-string:32");\n    if (raw == QStringLiteral("VISSTRING64")) return QStringLiteral("visible-string:64");\n    if (raw == QStringLiteral("VISSTRING65")) return QStringLiteral("visible-string:65");\n    if (raw == QStringLiteral("VISSTRING129")) return QStringLiteral("visible-string:129");\n    if (raw == QStringLiteral("VISSTRING255")) return QStringLiteral("visible-string:255");\n    if (raw == QStringLiteral("OBJREF")) return QStringLiteral("visible-string:129");\n    if (raw == QStringLiteral("UNICODE255")) return QStringLiteral("mms-string:255");\n    if (raw == QStringLiteral("OCTET64")) return QStringLiteral("octet-string:64");\n    if (raw == QStringLiteral("ENTRYID")) return QStringLiteral("octet-string:8");\n    if (raw == QStringLiteral("CHECK")) return QStringLiteral("bit-string:2");\n    if (raw == QStringLiteral("TCMD")) return QStringLiteral("integer:32");\n    if (raw == QStringLiteral("TRGOPS")) return QStringLiteral("bit-string:6");\n    if (raw == QStringLiteral("OPTFLDS")) return QStringLiteral("bit-string:10");\n    if (raw == QStringLiteral("CURRENCY")) return QStringLiteral("visible-string:3");\n    return {};\n}\n\n} // namespace detail\n\ninline QString sclMmsTypeSignature(const QString& rawType, const int count = 1) {\n    const auto scalar = detail::scalarMmsTypeSignature(rawType);\n    if (scalar.isEmpty()) {\n        throw std::runtime_error(\n            QStringLiteral("Unsupported SCL bType for exact MMS projection: %1")\n                .arg(rawType).toStdString());\n    }\n    return count > 1\n        ? QStringLiteral("array:%1:%2").arg(count).arg(scalar)\n        : scalar;\n}\n\nnamespace detail {''')
replace_once(path,
'''                lnTypes[currentLnType].dataObjects.push_back(DoDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("type")).toString()});''',
'''                lnTypes[currentLnType].dataObjects.push_back(DoDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("type")).toString(),\n                    elementCount(a.value(QStringLiteral("count")).toString())});''')
replace_once(path,
'''                doTypes[currentDoType].dataAttributes.push_back(DaDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("bType")).toString(),\n                    a.value(QStringLiteral("type")).toString(),\n                    a.value(QStringLiteral("fc")).toString()});''',
'''                doTypes[currentDoType].dataAttributes.push_back(DaDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("bType")).toString(),\n                    a.value(QStringLiteral("type")).toString(),\n                    a.value(QStringLiteral("fc")).toString(),\n                    elementCount(a.value(QStringLiteral("count")).toString())});''')
replace_once(path,
'''                doTypes[currentDoType].subDataObjects.push_back(SdoDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("type")).toString()});''',
'''                doTypes[currentDoType].subDataObjects.push_back(SdoDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("type")).toString(),\n                    elementCount(a.value(QStringLiteral("count")).toString())});''')
replace_once(path,
'''                daTypes[currentDaType].basicDataAttributes.push_back(BdaDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("bType")).toString(),\n                    a.value(QStringLiteral("type")).toString()});''',
'''                daTypes[currentDaType].basicDataAttributes.push_back(BdaDecl{\n                    a.value(QStringLiteral("name")).toString(),\n                    a.value(QStringLiteral("bType")).toString(),\n                    a.value(QStringLiteral("type")).toString(),\n                    elementCount(a.value(QStringLiteral("count")).toString())});''')
replace_once(path,
'''                             const QString& fc,\n                             const QString& cdc,\n                             const QString& rawType) {''',
'''                             const QString& fc,\n                             const QString& cdc,\n                             const QString& rawType,\n                             const int count) {''')
replace_once(path,
'''        value.rawType = rawType;\n        value.normalizedType = normalized;\n        value.initialValue = initialValue(normalized, daPath);''',
'''        value.rawType = rawType;\n        value.normalizedType = normalized;\n        value.mmsTypeSignature = sclMmsTypeSignature(rawType, count);\n        value.initialValue = initialValue(normalized, daPath);''')
replace_once(path,
'''            if (bda.bType.compare(QStringLiteral("Struct"), Qt::CaseInsensitive) == 0 && !bda.type.isEmpty()) {\n                expandDaType(instance, doPath, pathHere, fc, bda.type, depth + 1);\n            } else {\n                addLeaf(instance, doPath, pathHere, fc, QString{}, bda.bType);\n            }''',
'''            if (bda.bType.compare(QStringLiteral("Struct"), Qt::CaseInsensitive) == 0 && !bda.type.isEmpty()) {\n                if (bda.count != 1) {\n                    throw std::runtime_error("Arrays of structured BDA values are not supported by the bounded host model.");\n                }\n                expandDaType(instance, doPath, pathHere, fc, bda.type, depth + 1);\n            } else {\n                addLeaf(instance, doPath, pathHere, fc, QString{}, bda.bType, bda.count);\n            }''')
replace_once(path,
'''            if (da.bType.compare(QStringLiteral("Struct"), Qt::CaseInsensitive) == 0 && !da.type.isEmpty()) {\n                const auto before = result.values.size();\n                expandDaType(instance, doPath, da.name, da.fc, da.type, depth + 1);''',
'''            if (da.bType.compare(QStringLiteral("Struct"), Qt::CaseInsensitive) == 0 && !da.type.isEmpty()) {\n                if (da.count != 1) {\n                    throw std::runtime_error("Arrays of structured DA values are not supported by the bounded host model.");\n                }\n                const auto before = result.values.size();\n                expandDaType(instance, doPath, da.name, da.fc, da.type, depth + 1);''')
replace_once(path,
'''            } else {\n                addLeaf(instance, doPath, da.name, da.fc, cdc, da.bType);\n            }\n        }\n        for (const auto& sdo : it->subDataObjects) {\n            const auto nestedPath = doPath.isEmpty() ? sdo.name : doPath + QLatin1Char('.') + sdo.name;\n            expandDoType(instance, nestedPath, sdo.type, depth + 1);\n        }''',
'''            } else {\n                addLeaf(instance, doPath, da.name, da.fc, cdc, da.bType, da.count);\n            }\n        }\n        for (const auto& sdo : it->subDataObjects) {\n            if (sdo.count != 1) {\n                throw std::runtime_error("Arrays of SDO values are not supported by the bounded host model.");\n            }\n            const auto nestedPath = doPath.isEmpty() ? sdo.name : doPath + QLatin1Char('.') + sdo.name;\n            expandDoType(instance, nestedPath, sdo.type, depth + 1);\n        }''')
replace_once(path,
'''        for (const auto& dataObject : lnIt->dataObjects) {\n            expandDoType(instance, dataObject.name, dataObject.type, 0);\n        }''',
'''        for (const auto& dataObject : lnIt->dataObjects) {\n            if (dataObject.count != 1) {\n                throw std::runtime_error("Arrays of DO values are not supported by the bounded host model.");\n            }\n            expandDoType(instance, dataObject.name, dataObject.type, 0);\n        }''')
replace_range(path,
'''    std::sort(result.values.begin(), result.values.end(), [](const auto& left, const auto& right) {''',
'''    return result;\n}''',
'''    // Declaration order is part of the structured MMS TypeSpecification.\n    // Keep the SCL expansion order and only remove duplicate canonical leaves.\n    QSet<QString> seen;\n    std::vector<MaterializedSclValue> stable;\n    stable.reserve(result.values.size());\n    for (auto& value : result.values) {\n        const auto key = value.mmsDomain + QLatin1Char('\\n') + value.mmsItem;\n        if (seen.contains(key)) continue;\n        seen.insert(key);\n        stable.push_back(std::move(value));\n    }\n    result.values = std::move(stable);\n    return result;\n}\n''')

# ---------------------------------------------------------------------------
# Qt controller: manifest v4 is object-driven. No LN rows are emitted.
# Structural declaration order is retained, DataSets are bindings, and RCB
# definitions are exported for P2 to bind later.
# ---------------------------------------------------------------------------
path = "apps/ied_simulator/src/IedSimulatorController.cpp"
replace_once(path,
'''    item.insert(QStringLiteral("rawType"), value.rawType);\n    item.insert(QStringLiteral("iedName"), value.iedName);''',
'''    item.insert(QStringLiteral("rawType"), value.rawType);\n    item.insert(QStringLiteral("mmsTypeSignature"), value.mmsTypeSignature);\n    item.insert(QStringLiteral("iedName"), value.iedName);''')
replace_once(path,
'''                QStringLiteral("path"), QStringLiteral("rawType"), QStringLiteral("type"),\n                QStringLiteral("cdc"), QStringLiteral("quality"), QStringLiteral("timestamp"),''',
'''                QStringLiteral("path"), QStringLiteral("rawType"), QStringLiteral("type"),\n                QStringLiteral("mmsTypeSignature"), QStringLiteral("cdc"),\n                QStringLiteral("quality"), QStringLiteral("timestamp"),''')
new_manifest = r'''bool IedSimulatorController::writeModelManifest() {
    if (serverModelManifestPath_.isEmpty()) {
        serverModelManifestPath_ = QDir::temp().filePath(
            QStringLiteral("arstack-iedsim-%1.model")
                .arg(QCoreApplication::applicationPid()));
    }

    QSaveFile output{serverModelManifestPath_};
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("Could not create the MMS model manifest: %1")
                .arg(output.errorString()),
            QStringLiteral("Error"));
        return false;
    }

    ++modelRevision_;
    output.write("ARSTACK_IED_MODEL\t4\t");
    output.write(QByteArray::number(modelRevision_));
    output.write("\n");

    QSet<QString> emittedObjects;
    const auto emitObject = [&](const QVariantMap& item) {
        const auto domain = item.value(QStringLiteral("mmsDomain")).toString();
        const auto mmsItem = item.value(QStringLiteral("mmsItem")).toString();
        const auto signature = item.value(QStringLiteral("mmsTypeSignature")).toString();
        if (domain.isEmpty() || mmsItem.isEmpty() || signature.isEmpty()) return;
        const auto key = domain + QLatin1Char('\n') + mmsItem;
        if (emittedObjects.contains(key)) return;
        emittedObjects.insert(key);
        output.write("OBJ\t");
        output.write(manifestField(domain));
        output.write("\t");
        output.write(manifestField(mmsItem));
        output.write("\t");
        output.write(manifestField(item.value(QStringLiteral("rawType")).toString()));
        output.write("\t");
        output.write(manifestField(item.value(QStringLiteral("type")).toString()));
        output.write("\t");
        output.write(manifestField(signature));
        output.write("\t");
        output.write(manifestField(item.value(QStringLiteral("value")).toString()));
        output.write("\n");
    };

    // Preserve SCL declaration order for the recursive TypeSpecification while
    // reading the current value from the unified Qt runtime state.
    for (const auto& loaded : documents_) {
        for (const auto& variant : loaded.materializedValues) {
            const auto declared = variant.toMap();
            const auto key = runtimeValueKey(declared);
            const auto found = runtimeValues_.constFind(key);
            emitObject(found == runtimeValues_.cend() ? declared : *found);
        }
    }

    // Keep deterministic host-only extensions, but never let enrichment-only
    // entries redefine the structural SCL model without an exact type.
    QStringList remainingKeys;
    remainingKeys.reserve(runtimeValues_.size());
    for (auto it = runtimeValues_.cbegin(); it != runtimeValues_.cend(); ++it) {
        if (!emittedObjects.contains(it.value().value(QStringLiteral("mmsDomain")).toString() +
                                    QLatin1Char('\n') +
                                    it.value().value(QStringLiteral("mmsItem")).toString()) &&
            !it.value().value(QStringLiteral("mmsTypeSignature")).toString().isEmpty()) {
            remainingKeys.push_back(it.key());
        }
    }
    std::sort(remainingKeys.begin(), remainingKeys.end());
    for (const auto& key : remainingKeys) emitObject(runtimeValues_.value(key));

    for (const auto& loaded : documents_) {
        for (const auto& dataSet : loaded.document.data_sets) {
            const auto dataSetDomain = qstring(dataSet.ied_name) + qstring(dataSet.logical_device);
            const auto dataSetItem = qstring(dataSet.logical_node) + QLatin1Char('$') + qstring(dataSet.name);
            for (const auto& member : dataSet.entries) {
                const auto memberDomain = mmsDomainFor(member);
                const auto memberItem = mmsItemFor(member);
                if (dataSetDomain.isEmpty() || dataSetItem.isEmpty() ||
                    memberDomain.isEmpty() || memberItem.isEmpty()) continue;
                output.write("DS\t");
                output.write(manifestField(dataSetDomain));
                output.write("\t");
                output.write(manifestField(dataSetItem));
                output.write("\t");
                output.write(manifestField(memberDomain));
                output.write("\t");
                output.write(manifestField(memberItem));
                output.write("\n");
            }
        }

        for (const auto& report : loaded.document.report_controls) {
            const auto domain = qstring(report.ied_name) + qstring(report.logical_device);
            const auto ln = qstring(report.logical_node);
            const auto name = qstring(report.name);
            if (domain.isEmpty() || ln.isEmpty() || name.isEmpty()) continue;
            const auto item = ln + (report.buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$")) + name;
            const auto dataSetDomain = domain;
            const auto dataSetItem = ln + QLatin1Char('$') + qstring(report.data_set_name);
            output.write("RCB\t");
            output.write(manifestField(domain));
            output.write("\t");
            output.write(manifestField(item));
            output.write("\t");
            output.write(report.buffered ? "1" : "0");
            output.write("\t");
            output.write(manifestField(qstring(report.report_id)));
            output.write("\t");
            output.write(manifestField(dataSetDomain));
            output.write("\t");
            output.write(manifestField(dataSetItem));
            output.write("\t");
            output.write(QByteArray::number(report.configuration_revision.value_or(1U)));
            output.write("\t");
            output.write(QByteArray::number(report.buffer_time_ms.value_or(0U)));
            output.write("\t");
            output.write(QByteArray::number(report.integrity_period_ms.value_or(0U)));
            output.write("\t");
            output.write(report.indexed ? "1" : "0");
            output.write("\n");
        }
    }

    if (emittedObjects.isEmpty()) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("The imported SCL produced no exact structural MMS objects."),
            QStringLiteral("Error"));
        output.cancelWriting();
        return false;
    }

    if (!output.commit()) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("Could not publish the MMS model manifest: %1")
                .arg(output.errorString()),
            QStringLiteral("Error"));
        return false;
    }
    return true;
}

'''
replace_range(path,
'''bool IedSimulatorController::writeModelManifest() {''',
'''QString IedSimulatorController::ensureMmsServerProgram()''',
new_manifest)

# ---------------------------------------------------------------------------
# Host server: manifest v4 exact type parser + object-driven roots + RCB model
# projection. Legacy v2/v3 manifests remain readable for existing tests/tools.
# ---------------------------------------------------------------------------
path = "tools/static_ied_server.cpp"
replace_once(path,
'''    std::string raw_type;\n    std::string normalized_type;\n    std::string text;''',
'''    std::string raw_type;\n    std::string normalized_type;\n    std::string type_signature;\n    std::string text;''')
replace_once(path,
'''struct ManifestTypeNode final {\n    std::map<std::string, ManifestTypeNode> children;\n    std::optional<std::size_t> value_index;\n};''',
'''struct ManifestTypeNode final {\n    std::vector<std::pair<std::string, ManifestTypeNode>> children;\n    std::optional<std::size_t> value_index;\n};\n\n[[nodiscard]] ManifestTypeNode& manifest_child(\n    ManifestTypeNode& node, const std::string& name) {\n    for (auto& [child_name, child] : node.children) {\n        if (child_name == name) return child;\n    }\n    node.children.emplace_back(name, ManifestTypeNode{});\n    return node.children.back().second;\n}''')
new_types = r'''[[nodiscard]] mms::MmsTypeSpecification manifest_type(
    const std::string& raw_type,
    const std::string& normalized_type,
    std::string name = {}) {
    const auto raw = upper_copy(raw_type);
    const auto normalized = upper_copy(normalized_type);
    mms::MmsTypeSpecification result;
    result.name = std::move(name);
    if (normalized == "ENUMERATION" || raw.find("ENUM") != std::string::npos) {
        result.kind = mms::MmsTypeKind::integer;
        result.size = 32U;
    } else if (normalized == "BOOLEAN" || raw.find("BOOL") != std::string::npos) {
        result.kind = mms::MmsTypeKind::boolean;
    } else if (normalized == "QUALITY") {
        result.kind = mms::MmsTypeKind::bit_string;
        result.size = 13U;
    } else if (normalized == "TIMESTAMP") {
        result.kind = mms::MmsTypeKind::utc_time;
    } else if (raw.find("FLOAT64") != std::string::npos) {
        result.kind = mms::MmsTypeKind::floating_point;
        result.size = 64U;
        result.exponent_width = 11U;
    } else if (raw.find("FLOAT") != std::string::npos) {
        result.kind = mms::MmsTypeKind::floating_point;
        result.size = 32U;
        result.exponent_width = 8U;
    } else if (raw.find("INT") != std::string::npos &&
               (raw.ends_with('U') || raw.find("UINT") != std::string::npos)) {
        result.kind = mms::MmsTypeKind::unsigned_integer;
        result.size = 32U;
    } else if (normalized == "NUMBER" || raw.find("INT") != std::string::npos) {
        result.kind = mms::MmsTypeKind::integer;
        result.size = 32U;
    } else {
        result.kind = mms::MmsTypeKind::visible_string;
        result.size = 255U;
    }
    return result;
}

[[nodiscard]] std::uint32_t signature_size(
    const std::string_view text, const std::string_view prefix) {
    if (!text.starts_with(prefix)) {
        throw std::runtime_error("Invalid exact MMS type signature: " + std::string{text});
    }
    const auto suffix = text.substr(prefix.size());
    if (suffix.empty()) {
        throw std::runtime_error("Missing size in exact MMS type signature: " + std::string{text});
    }
    const auto value = std::stoull(std::string{suffix});
    if (value == 0U || value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Invalid size in exact MMS type signature: " + std::string{text});
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] mms::MmsTypeSpecification exact_manifest_type(
    const std::string& signature,
    std::string name = {}) {
    mms::MmsTypeSpecification result;
    result.name = std::move(name);
    if (signature == "boolean") {
        result.kind = mms::MmsTypeKind::boolean;
        return result;
    }
    if (signature == "utc-time") {
        result.kind = mms::MmsTypeKind::utc_time;
        return result;
    }
    if (signature.starts_with("integer:")) {
        result.kind = mms::MmsTypeKind::integer;
        result.size = signature_size(signature, "integer:");
        return result;
    }
    if (signature.starts_with("unsigned-integer:")) {
        result.kind = mms::MmsTypeKind::unsigned_integer;
        result.size = signature_size(signature, "unsigned-integer:");
        return result;
    }
    if (signature.starts_with("bit-string:")) {
        result.kind = mms::MmsTypeKind::bit_string;
        result.size = signature_size(signature, "bit-string:");
        return result;
    }
    if (signature.starts_with("octet-string:")) {
        result.kind = mms::MmsTypeKind::octet_string;
        result.size = signature_size(signature, "octet-string:");
        return result;
    }
    if (signature.starts_with("visible-string:")) {
        result.kind = mms::MmsTypeKind::visible_string;
        result.size = signature_size(signature, "visible-string:");
        return result;
    }
    if (signature.starts_with("mms-string:")) {
        result.kind = mms::MmsTypeKind::mms_string;
        result.size = signature_size(signature, "mms-string:");
        return result;
    }
    if (signature.starts_with("floating-point:")) {
        const auto parts = split_fields(signature, ':');
        if (parts.size() != 3U) {
            throw std::runtime_error("Invalid floating-point exact MMS type signature: " + signature);
        }
        result.kind = mms::MmsTypeKind::floating_point;
        result.size = static_cast<std::uint32_t>(std::stoul(parts[1]));
        result.exponent_width = static_cast<std::uint32_t>(std::stoul(parts[2]));
        return result;
    }
    if (signature.starts_with("array:")) {
        const auto first = signature.find(':', 6U);
        if (first == std::string::npos || first + 1U >= signature.size()) {
            throw std::runtime_error("Invalid array exact MMS type signature: " + signature);
        }
        const auto count = std::stoull(signature.substr(6U, first - 6U));
        if (count == 0U || count > 65'535U) {
            throw std::runtime_error("Invalid array bound in exact MMS type signature: " + signature);
        }
        result.kind = mms::MmsTypeKind::array;
        result.size = static_cast<std::uint32_t>(count);
        result.children.push_back(exact_manifest_type(signature.substr(first + 1U)));
        return result;
    }
    throw std::runtime_error("Unsupported exact MMS type signature: " + signature);
}

[[nodiscard]] mms::MmsDataValue manifest_data(
    const mms::MmsTypeSpecification& type,
    const std::string& text) {
    switch (type.kind) {
    case mms::MmsTypeKind::array: {
        if (type.children.size() != 1U) return mms::MmsDataValue::array({});
        std::vector<mms::MmsDataValue> children;
        children.reserve(type.size.value_or(0U));
        for (std::uint32_t index = 0U; index < type.size.value_or(0U); ++index) {
            children.push_back(manifest_data(type.children.front(), text));
        }
        return mms::MmsDataValue::array(std::move(children));
    }
    case mms::MmsTypeKind::boolean:
        return mms::MmsDataValue::boolean(text_boolean(text));
    case mms::MmsTypeKind::bit_string: {
        const auto bits = type.size.value_or(0U);
        const auto bytes = static_cast<std::size_t>((bits + 7U) / 8U);
        std::vector<std::uint8_t> raw(bytes, 0U);
        const auto unused = static_cast<std::uint8_t>(bytes == 0U ? 0U : bytes * 8U - bits);
        return mms::MmsDataValue::bit_string(unused, raw);
    }
    case mms::MmsTypeKind::integer:
        return mms::MmsDataValue::integer(text_integer(text));
    case mms::MmsTypeKind::unsigned_integer:
        return mms::MmsDataValue::unsigned_integer(
            static_cast<std::uint64_t>(std::max<std::int64_t>(0, text_integer(text))));
    case mms::MmsTypeKind::floating_point:
        try {
            return type.size.value_or(32U) == 64U
                ? mms::MmsDataValue::floating_point(std::stod(text))
                : mms::MmsDataValue::floating_point(std::stof(text));
        } catch (...) {
            return type.size.value_or(32U) == 64U
                ? mms::MmsDataValue::floating_point(0.0)
                : mms::MmsDataValue::floating_point(0.0F);
        }
    case mms::MmsTypeKind::octet_string: {
        std::vector<std::uint8_t> bytes(type.size.value_or(0U), 0U);
        return mms::MmsDataValue::octet_string(bytes);
    }
    case mms::MmsTypeKind::mms_string:
        return mms::MmsDataValue::mms_string(text == "---" ? std::string{} : text);
    case mms::MmsTypeKind::utc_time:
        return mms::MmsDataValue::utc_time(
            mms::Iec61850UtcTime{std::chrono::system_clock::time_point{}, 0U});
    default:
        return mms::MmsDataValue::visible_string(text == "---" ? std::string{} : text);
    }
}

void encode_manifest_value(ManifestValue& value) {
    value.type = value.type_signature.empty()
        ? manifest_type(value.raw_type, value.normalized_type)
        : exact_manifest_type(value.type_signature);
    value.data = manifest_data(value.type, value.text);
    value.type_specification = mms::MmsServiceCodec::encode_type_specification(value.type);
    value.encoded = mms::MmsDataCodec::encode(*value.data);
}

'''
replace_range(path,
'''[[nodiscard]] mms::MmsTypeSpecification manifest_type(''',
'''[[nodiscard]] wire::EncodeResult read_manifest_value(''',
new_types)

new_loader = r'''[[nodiscard]] ManifestModel load_manifest_model(
    const std::string& path,
    const std::span<const std::uint8_t> fallback_type_specification,
    const EncodedValue& fallback_value) {
    ManifestModel model;
    model.path = path;
    if (path.empty()) return model;

    std::ifstream input{path};
    if (!input) throw std::runtime_error("Could not open model manifest: " + path);

    struct ParsedObject final {
        std::string domain;
        std::string item;
        std::string raw_type;
        std::string normalized_type;
        std::string type_signature;
        std::string text;
    };
    struct ParsedDataSetMember final {
        std::string domain;
        std::string item;
        std::string member_domain;
        std::string member_item;
    };
    struct ParsedRcb final {
        std::string domain;
        std::string item;
        bool buffered{};
        std::string report_id;
        std::string data_set_domain;
        std::string data_set_item;
        std::uint32_t conf_rev{1U};
        std::uint32_t buffer_time_ms{};
        std::uint32_t integrity_period_ms{};
        bool indexed{};
    };

    std::vector<std::pair<std::string, std::string>> roots;
    std::vector<ParsedObject> parsed_objects;
    std::vector<ParsedDataSetMember> parsed_members;
    std::vector<ParsedRcb> parsed_rcbs;
    std::set<std::pair<std::string, std::string>> unique_roots;
    std::set<std::pair<std::string, std::string>> unique_objects;
    std::string line;
    std::uint32_t manifest_version{};
    if (std::getline(input, line)) {
        const auto header_fields = split_fields(line, '\t');
        model.revision = manifest_revision(line);
        if (header_fields.size() >= 2U) {
            try { manifest_version = static_cast<std::uint32_t>(std::stoul(header_fields[1])); }
            catch (...) { manifest_version = 0U; }
        }
    }
    while (std::getline(input, line)) {
        const auto fields = split_fields(line, '\t');
        if (fields.size() >= 3U && fields[0] == "LN") {
            ++model.declared_entries;
            if (!fields[1].empty() && !fields[2].empty() &&
                unique_roots.emplace(fields[1], fields[2]).second) {
                roots.emplace_back(fields[1], fields[2]);
            }
        } else if (fields[0] == "OBJ" && fields.size() >= 6U) {
            ++model.declared_entries;
            if (fields[1].empty() || fields[2].empty() ||
                !unique_objects.emplace(fields[1], fields[2]).second) continue;
            if (manifest_version >= 4U) {
                if (fields.size() < 7U || fields[5].empty()) {
                    throw std::runtime_error("Manifest v4 OBJ is missing exact MMS type metadata.");
                }
                parsed_objects.push_back({
                    fields[1], fields[2], fields[3], fields[4], fields[5], fields[6]});
            } else {
                parsed_objects.push_back({
                    fields[1], fields[2], fields[3], fields[4], {}, fields[5]});
            }
        } else if (fields.size() >= 5U && fields[0] == "DS") {
            parsed_members.push_back({fields[1], fields[2], fields[3], fields[4]});
        } else if (fields.size() >= 11U && fields[0] == "RCB") {
            ParsedRcb rcb;
            rcb.domain = fields[1];
            rcb.item = fields[2];
            rcb.buffered = fields[3] == "1";
            rcb.report_id = fields[4];
            rcb.data_set_domain = fields[5];
            rcb.data_set_item = fields[6];
            try { rcb.conf_rev = static_cast<std::uint32_t>(std::stoul(fields[7])); } catch (...) {}
            try { rcb.buffer_time_ms = static_cast<std::uint32_t>(std::stoul(fields[8])); } catch (...) {}
            try { rcb.integrity_period_ms = static_cast<std::uint32_t>(std::stoul(fields[9])); } catch (...) {}
            rcb.indexed = fields[10] == "1";
            if (!rcb.domain.empty() && !rcb.item.empty()) parsed_rcbs.push_back(std::move(rcb));
        }
    }

    const auto add_rcb_object = [&](const ParsedRcb& rcb,
                                    const std::string& suffix,
                                    const std::string& signature,
                                    const std::string& text) {
        const auto item = rcb.item + "$" + suffix;
        if (!unique_objects.emplace(rcb.domain, item).second) return;
        parsed_objects.push_back({rcb.domain, item, {}, {}, signature, text});
    };
    for (const auto& rcb : parsed_rcbs) {
        const auto data_set_ref = rcb.data_set_domain.empty()
            ? rcb.data_set_item
            : rcb.data_set_domain + "/" + rcb.data_set_item;
        add_rcb_object(rcb, "RptID", "visible-string:255", rcb.report_id);
        add_rcb_object(rcb, "RptEna", "boolean", "false");
        add_rcb_object(rcb, "DatSet", "visible-string:255", data_set_ref);
        add_rcb_object(rcb, "ConfRev", "unsigned-integer:32", std::to_string(rcb.conf_rev));
        add_rcb_object(rcb, "OptFlds", "bit-string:10", "0");
        add_rcb_object(rcb, "BufTm", "unsigned-integer:32", std::to_string(rcb.buffer_time_ms));
        add_rcb_object(rcb, "TrgOps", "bit-string:6", "0");
        add_rcb_object(rcb, "IntgPd", "unsigned-integer:32", std::to_string(rcb.integrity_period_ms));
        if (rcb.buffered) {
            add_rcb_object(rcb, "PurgeBuf", "boolean", "false");
            add_rcb_object(rcb, "EntryID", "octet-string:8", "");
            add_rcb_object(rcb, "TimeOfEntry", "utc-time", "1970-01-01 00:00:00.000");
        } else {
            add_rcb_object(rcb, "Resv", "boolean", "false");
            add_rcb_object(rcb, "GI", "boolean", "false");
            add_rcb_object(rcb, "SqNum", "unsigned-integer:32", "0");
        }
        ++model.declared_entries;
    }

    // Manifest v4 is object-driven: derive logical-node roots from the first
    // item component instead of requiring a separate LN scaffold.
    if (manifest_version >= 4U) {
        for (const auto& object : parsed_objects) {
            const auto parts = split_fields(object.item, '$');
            if (parts.empty() || parts[0].empty()) continue;
            if (unique_roots.emplace(object.domain, parts[0]).second) {
                roots.emplace_back(object.domain, parts[0]);
            }
        }
    }
    if (roots.empty()) {
        throw std::runtime_error("Model manifest contains no usable structural MMS objects.");
    }

    model.values.reserve(std::min<std::size_t>(
        kHostMaximumManifestObjects, roots.size() + parsed_objects.size()));
    model.root_trees.reserve(roots.size());
    model.root_value_indices.reserve(roots.size());
    std::unordered_map<std::string, std::size_t> root_indices;
    for (const auto& [domain, item] : roots) {
        if (model.values.size() >= kHostMaximumManifestObjects) break;
        ManifestValue root;
        root.domain = domain;
        root.item = item;
        root.root = true;
        root.type_specification.assign(
            fallback_type_specification.begin(), fallback_type_specification.end());
        root.encoded.assign(fallback_value.bytes.begin(), fallback_value.bytes.end());
        const auto value_index = model.values.size();
        model.values.push_back(std::move(root));
        root_indices.emplace(object_key(domain, item), model.root_trees.size());
        model.root_value_indices.push_back(value_index);
        model.root_trees.emplace_back();
    }

    for (const auto& parsed : parsed_objects) {
        if (model.values.size() >= kHostMaximumManifestObjects) break;
        const auto key = object_key(parsed.domain, parsed.item);
        if (model.value_indices.contains(key)) continue;
        ManifestValue value;
        value.domain = parsed.domain;
        value.item = parsed.item;
        value.raw_type = parsed.raw_type;
        value.normalized_type = parsed.normalized_type;
        value.type_signature = parsed.type_signature;
        value.text = parsed.text;
        encode_manifest_value(value);
        const auto value_index = model.values.size();
        model.values.push_back(std::move(value));
        model.value_indices.emplace(key, value_index);

        const auto parts = split_fields(parsed.item, '$');
        if (parts.size() < 2U) continue;
        const auto found_root = root_indices.find(object_key(parsed.domain, parts[0]));
        if (found_root == root_indices.end()) continue;
        auto* node = &model.root_trees[found_root->second];
        for (std::size_t part = 1U; part < parts.size(); ++part) {
            node = &manifest_child(*node, parts[part]);
        }
        node->value_index = value_index;
    }
    rebuild_manifest_roots(model);

    model.objects.reserve(model.values.size());
    for (auto& value : model.values) {
        model.objects.push_back(mms::MmsStaticObjectEntry{
            value.domain,
            value.item,
            value.type_specification,
            read_manifest_value,
            &value});
    }

    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, std::string>>>
        grouped_members;
    for (const auto& member : parsed_members) {
        if (!model.value_indices.contains(object_key(member.member_domain, member.member_item))) {
            continue;
        }
        grouped_members[{member.domain, member.item}].emplace_back(
            member.member_domain, member.member_item);
    }
    model.data_set_storage.reserve(std::min<std::size_t>(
        grouped_members.size(), kHostMaximumManifestDataSets));
    for (auto& [name, members] : grouped_members) {
        if (model.data_set_storage.size() >= kHostMaximumManifestDataSets) break;
        if (members.empty()) continue;
        ManifestDataSetStorage storage;
        storage.domain = std::move(name.first);
        storage.item = std::move(name.second);
        storage.member_names = std::move(members);
        model.data_set_storage.push_back(std::move(storage));
    }
    model.data_sets.reserve(model.data_set_storage.size());
    for (auto& storage : model.data_set_storage) {
        storage.members.reserve(storage.member_names.size());
        for (const auto& [domain, item] : storage.member_names) {
            storage.members.push_back({domain, item});
        }
        model.data_sets.push_back({
            storage.domain, storage.item, storage.members, false});
    }
    return model;
}

'''
replace_range(path,
'''[[nodiscard]] ManifestModel load_manifest_model(''',
'''[[nodiscard]] std::size_t refresh_manifest_values(''',
new_loader)
replace_once(path,
'''        if (fields.size() < 6U || fields[0] != "OBJ") continue;\n        const auto found = model.value_indices.find(object_key(fields[1], fields[2]));\n        if (found == model.value_indices.end()) continue;\n        auto& value = model.values[found->second];\n        if (value.text == fields[5]) continue;\n        value.text = fields[5];''',
'''        if (fields.size() < 6U || fields[0] != "OBJ") continue;\n        const auto found = model.value_indices.find(object_key(fields[1], fields[2]));\n        if (found == model.value_indices.end()) continue;\n        auto& value = model.values[found->second];\n        const auto text_index = fields.size() >= 7U ? 6U : 5U;\n        if (value.text == fields[text_index]) continue;\n        value.text = fields[text_index];''')

# ---------------------------------------------------------------------------
# Regression fixture: add exact-width types, declaration-order evidence, and a
# real SCL URCB definition bound to the existing DataSet.
# ---------------------------------------------------------------------------
path = "tests/fixtures/scl/iedsim-full-model.scd"
replace_once(path,
'''        <DataSet name="dsSV">\n          <FCDA ldInst="LD0" lnClass="TCTR" lnInst="1" doName="Amp" daName="instMag.i" fc="MX"/>\n        </DataSet>''',
'''        <DataSet name="dsSV">\n          <FCDA ldInst="LD0" lnClass="TCTR" lnInst="1" doName="Amp" daName="instMag.i" fc="MX"/>\n        </DataSet>\n        <ReportControl name="urcb01" datSet="dsSV" rptID="MU01_LD0_URCB01" buffered="false" bufTime="20" intgPd="1000" confRev="7" indexed="false"/>''')
replace_once(path,
'''    <DAType id="DA_TCTR_Amp">\n      <BDA name="i" bType="INT32"/>\n    </DAType>''',
'''    <DAType id="DA_TCTR_Amp">\n      <BDA name="i" bType="INT16"/>\n      <BDA name="range" bType="INT8U"/>\n    </DAType>''')

# ---------------------------------------------------------------------------
# End-to-end Qt test: manifest v4, no LN rows, exact type signatures, RCB
# definition projection, structural-only leaf, DataSet binding, and live read.
# ---------------------------------------------------------------------------
path = "apps/ied_simulator/test_gui_live_value.py"
replace_once(path,
'''            if parts[0] == "ARSTACK_IED_MODEL" and len(parts) >= 3:\n                if int(parts[2]) >= 2:\n                    return text''',
'''            if parts[0] == "ARSTACK_IED_MODEL" and len(parts) >= 3:\n                if parts[1] == "4" and int(parts[2]) >= 2:\n                    return text''')
replace_once(path,
'''        if "OBJ\\tMU01LD0\\tTCTR1$MX$Amp$instMag$i\\tINT32\\tNumber\\t42" not in manifest:\n            raise RuntimeError("edited TCTR leaf is missing from manifest")\n        if "OBJ\\tMU01LD0\\tXCBR1$ST$Health$stVal\\tBOOLEAN\\tBoolean\\tfalse" not in manifest:\n            raise RuntimeError("structural-only XCBR leaf is missing from manifest")''',
'''        if "\\nLN\\t" in "\\n" + manifest:\n            raise RuntimeError("manifest v4 still depends on LN scaffold rows")\n        if "OBJ\\tMU01LD0\\tTCTR1$MX$Amp$instMag$i\\tINT16\\tNumber\\tinteger:16\\t42" not in manifest:\n            raise RuntimeError("edited INT16 TCTR leaf or exact type is missing from manifest")\n        if "OBJ\\tMU01LD0\\tTCTR1$MX$Amp$instMag$range\\tINT8U\\tNumber\\tunsigned-integer:8\\t0" not in manifest:\n            raise RuntimeError("ordered INT8U structured member or exact type is missing")\n        if manifest.index("TCTR1$MX$Amp$instMag$i") > manifest.index("TCTR1$MX$Amp$instMag$range"):\n            raise RuntimeError("SCL declaration order was not preserved")\n        if "OBJ\\tMU01LD0\\tXCBR1$ST$Health$stVal\\tBOOLEAN\\tBoolean\\tboolean\\tfalse" not in manifest:\n            raise RuntimeError("structural-only XCBR leaf is missing from manifest")\n        if "DS\\tMU01LD0\\tLLN0$dsSV\\tMU01LD0\\tTCTR1$MX$Amp$instMag$i" not in manifest:\n            raise RuntimeError("DataSet binding is missing from manifest")\n        if "RCB\\tMU01LD0\\tLLN0$RP$urcb01\\t0\\tMU01_LD0_URCB01\\tMU01LD0\\tLLN0$dsSV\\t7\\t20\\t1000\\t0" not in manifest:\n            raise RuntimeError("SCL URCB definition is missing from manifest")''')
replace_once(path,
'''            f"IEDSIM_FULL_MODEL_LIVE_VALUE_PASS port={port} value=42 "\n            "structural=XCBR1$ST$Health$stVal"''',
'''            f"IEDSIM_P0_FULL_MODEL_PASS port={port} value=42 "\n            "exact=integer:16 dataset=LLN0$dsSV rcb=LLN0$RP$urcb01 "\n            "structural=XCBR1$ST$Health$stVal"''')

print("P0 patch applied")
