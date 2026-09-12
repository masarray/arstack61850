#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def replace_between(path: str, start: str, end: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    first = text.find(start)
    if first < 0:
        raise SystemExit(f"{path}: start anchor not found: {start!r}")
    last = text.find(end, first)
    if last < 0:
        raise SystemExit(f"{path}: end anchor not found: {end!r}")
    p.write_text(text[:first] + replacement + text[last:], encoding="utf-8")


# Core build integration.
replace_once(
    "CMakeLists.txt",
    "    src/scl/dataset_reference.cpp\n    src/scl/parser.cpp\n    src/comtrade/model.cpp\n",
    "    src/scl/dataset_reference.cpp\n    src/scl/parser.cpp\n"
    "    src/simulation/ied_simulator_profile.cpp\n"
    "    src/simulation/ied_simulator_engine.cpp\n"
    "    src/comtrade/model.cpp\n",
)
replace_once(
    "CMakeLists.txt",
    "    add_test(NAME ariec61850_scl_tests COMMAND ariec61850_scl_tests)\n\n"
    "    add_executable(ariec61850_comtrade_tests tests/test_comtrade.cpp)\n",
    "    add_test(NAME ariec61850_scl_tests COMMAND ariec61850_scl_tests)\n\n"
    "    add_executable(ariec61850_simulation_tests tests/test_simulation.cpp)\n"
    "    target_link_libraries(ariec61850_simulation_tests PRIVATE ARIEC61850::core)\n"
    "    target_compile_definitions(ariec61850_simulation_tests PRIVATE\n"
    "        ARIEC61850_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")\n"
    "    target_compile_features(ariec61850_simulation_tests PRIVATE cxx_std_20)\n"
    "    ariec61850_apply_warnings(ariec61850_simulation_tests)\n"
    "    ariec61850_apply_sanitizers(ariec61850_simulation_tests)\n"
    "    add_test(NAME ariec61850_simulation_tests COMMAND ariec61850_simulation_tests)\n\n"
    "    add_executable(ariec61850_comtrade_tests tests/test_comtrade.cpp)\n",
)

# Header/source strict-build cleanup and deterministic source ordering.
replace_once(
    "include/ariec61850/simulation/ied_simulator_profile.hpp",
    "    double phase_degrees{};\n    bool dynamic{};\n",
    "    double phase_degrees{};\n    std::size_t source_order{};\n    bool dynamic{};\n",
)
replace_once(
    "src/simulation/ied_simulator_profile.cpp",
    "#include <iomanip>\n#include <map>\n#include <set>\n",
    "#include <iomanip>\n#include <locale>\n#include <map>\n#include <optional>\n#include <set>\n",
)
replace_once(
    "src/simulation/ied_simulator_profile.cpp",
    "    std::set<std::string> point_keys;\n    const auto append_entry = [&](const scl::SclDataSetEntry& entry, const bool structural) {\n",
    "    std::set<std::string> point_keys;\n    std::size_t source_order{};\n"
    "    const auto append_entry = [&](const scl::SclDataSetEntry& entry, const bool structural) {\n",
)
replace_once(
    "src/simulation/ied_simulator_profile.cpp",
    "        auto point = make_point(entry, source_ied, runtime_ied);\n        const auto key = point.mms_domain + \"\\n\" + point.mms_item;\n",
    "        auto point = make_point(entry, source_ied, runtime_ied);\n"
    "        point.source_order = source_order++;\n"
    "        const auto key = point.mms_domain + \"\\n\" + point.mms_item;\n",
)

# Qt adapter consumes the portable profile builder rather than independently
# deciding which SCL leaves exist or how their runtime defaults are classified.
replace_once(
    "apps/ied_simulator/src/IedSimulatorController.hpp",
    '#include "ariec61850/scl/model.hpp"\n',
    '#include "ariec61850/scl/model.hpp"\n'
    '#include "ariec61850/simulation/ied_simulator_profile.hpp"\n',
)
replace_once(
    "apps/ied_simulator/src/IedSimulatorController.hpp",
    "    [[nodiscard]] static QVariantMap valueMap(\n"
    "        const ar::iec61850::scl::SclDataSetEntry& entry);\n",
    "    [[nodiscard]] static QVariantMap valueMap(\n"
    "        const ar::iec61850::simulation::IedSimulatorPoint& point);\n",
)

replace_between(
    "apps/ied_simulator/src/IedSimulatorController.cpp",
    "QString normalizedType(",
    "std::optional<int> controlModelCode",
    "",
)
replace_between(
    "apps/ied_simulator/src/IedSimulatorController.cpp",
    "QString initialValue(",
    "QString referenceFor(",
    "",
)

new_runtime_adapter = r'''void IedSimulatorController::rebuildValues() {
    values_.clear();
    selectedValueIndex_ = -1;
    if (selectedIedIndex_ < 0 || selectedIedIndex_ >= ieds_.size()) {
        emit valuesChanged();
        return;
    }
    const auto ied = ieds_.at(selectedIedIndex_).toMap();
    const int documentIndex = ied.value(QStringLiteral("documentIndex")).toInt();
    if (documentIndex < 0 || documentIndex >= static_cast<int>(documents_.size())) {
        emit valuesChanged();
        return;
    }

    ar::iec61850::simulation::IedSimulatorProfileFromSclOptions options;
    options.ied_name = ied.value(QStringLiteral("name")).toString().toStdString();
    options.runtime_ied_name = options.ied_name;
    const auto built = ar::iec61850::simulation::IedSimulatorProfileBuilder::build(
        documents_[static_cast<std::size_t>(documentIndex)].document,
        options);

    std::vector<const ar::iec61850::simulation::IedSimulatorPoint*> points;
    points.reserve(built.profile.point_count());
    for (const auto& device : built.profile.logical_devices) {
        for (const auto& node : device.logical_nodes) {
            for (const auto& point : node.points) points.push_back(&point);
        }
    }
    std::stable_sort(
        points.begin(), points.end(),
        [](const auto* left, const auto* right) {
            return left->source_order < right->source_order;
        });

    for (const auto* point : points) {
        const auto key = qstring(point->reference);
        if (!runtimeValues_.contains(key)) runtimeValues_.insert(key, valueMap(*point));
        values_.push_back(runtimeValues_.value(key));
    }

    selectedValueIndex_ = values_.isEmpty() ? -1 : 0;
    emit valuesChanged();
}

void IedSimulatorController::seedRuntimeValues() {
    if (selectedIedIndex_ < 0 || selectedIedIndex_ >= ieds_.size()) return;
    const auto ied = ieds_.at(selectedIedIndex_).toMap();
    const int documentIndex = ied.value(QStringLiteral("documentIndex")).toInt();
    if (documentIndex < 0 || documentIndex >= static_cast<int>(documents_.size())) return;

    ar::iec61850::simulation::IedSimulatorProfileFromSclOptions options;
    options.ied_name = ied.value(QStringLiteral("name")).toString().toStdString();
    options.runtime_ied_name = options.ied_name;
    const auto built = ar::iec61850::simulation::IedSimulatorProfileBuilder::build(
        documents_[static_cast<std::size_t>(documentIndex)].document,
        options);
    for (const auto& device : built.profile.logical_devices) {
        for (const auto& node : device.logical_nodes) {
            for (const auto& point : node.points) {
                const auto key = qstring(point.reference);
                if (!runtimeValues_.contains(key)) runtimeValues_.insert(key, valueMap(point));
            }
        }
    }
}

QVariantMap IedSimulatorController::valueMap(
    const ar::iec61850::simulation::IedSimulatorPoint& point) {
    QVariantMap item;
    const auto dataObject = qstring(point.data_object);
    const auto dataAttribute = qstring(point.data_attribute);
    item.insert(
        QStringLiteral("name"),
        dataAttribute.isEmpty() ? dataObject : dataObject + QLatin1Char('.') + dataAttribute);
    item.insert(QStringLiteral("reference"), qstring(point.reference));
    item.insert(QStringLiteral("logicalDevice"), qstring(point.logical_device));
    item.insert(QStringLiteral("logicalNode"), qstring(point.logical_node));
    item.insert(QStringLiteral("dataObject"), dataObject);
    item.insert(QStringLiteral("dataAttribute"), dataAttribute);
    item.insert(QStringLiteral("fc"), qstring(point.functional_constraint));
    item.insert(QStringLiteral("cdc"), qstring(point.cdc));
    item.insert(QStringLiteral("type"), qstring(point.display_type));
    item.insert(QStringLiteral("rawType"), qstring(point.basic_type));
    item.insert(QStringLiteral("iedName"), qstring(point.ied_name));
    item.insert(QStringLiteral("mmsDomain"), qstring(point.mms_domain));
    item.insert(QStringLiteral("mmsItem"), qstring(point.mms_item));
    item.insert(QStringLiteral("value"), qstring(point.initial_value));
    item.insert(QStringLiteral("quality"), QStringLiteral("Good"));
    item.insert(QStringLiteral("origin"), QStringLiteral("Simulator"));
    item.insert(QStringLiteral("writable"), true);
    item.insert(QStringLiteral("changed"), false);
    item.insert(QStringLiteral("updated"), QStringLiteral("—"));
    if (point.display_type == "Enumeration" && point.cdc == "DPC") {
        item.insert(
            QStringLiteral("options"),
            QStringList{
                QStringLiteral("intermediate-state"),
                QStringLiteral("off"),
                QStringLiteral("on"),
                QStringLiteral("bad-state")});
    } else if (point.display_type == "Boolean") {
        item.insert(
            QStringLiteral("options"),
            QStringList{QStringLiteral("false"), QStringLiteral("true")});
    }
    return item;
}

'''
replace_between(
    "apps/ied_simulator/src/IedSimulatorController.cpp",
    "void IedSimulatorController::rebuildValues() {",
    "void IedSimulatorController::appendActivity(",
    new_runtime_adapter,
)

# The old implementation used this include only for a per-function de-dup set.
replace_once(
    "apps/ied_simulator/src/IedSimulatorController.cpp",
    "#include <unordered_set>\n",
    "",
)

print("Native ARIEC simulator profile/engine integrated into ARStack core and Qt adapter.")
