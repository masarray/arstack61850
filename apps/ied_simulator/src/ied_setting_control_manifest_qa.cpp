// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedSettingControlManifest.hpp"

#include <QCoreApplication>
#include <QSet>

#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) throw std::runtime_error(std::string{"CHECK failed: "} + #condition); \
} while (false)

int main(int argc, char** argv) {
    QCoreApplication application{argc, argv};
    try {
        using ar::iec61850::scl::SclSettingControl;
        SclSettingControl valid;
        valid.ied_name = "SGIED";
        valid.ld_inst = "LD0";
        valid.logical_node_path = "LLN0";
        valid.control_block_reference = "SGIEDLD0/LLN0$SP$SGCB";
        valid.number_of_setting_groups = 4U;
        valid.active_setting_group = 1U;

        QSet<QString> emitted;
        const auto line = arstack::iedsim::settingControlManifestLine(
            valid, QStringLiteral("SGIED"), emitted);
        CHECK(line == QByteArray{"SGCB\tSGIEDLD0\tLLN0$SP$SGCB\t4\t1\n"});
        CHECK(arstack::iedsim::settingControlManifestLine(
            valid, QStringLiteral("SGIED"), emitted).isEmpty());

        QSet<QString> wrongIed;
        CHECK(arstack::iedsim::settingControlManifestLine(
            valid, QStringLiteral("OTHER"), wrongIed).isEmpty());

        auto invalid = valid;
        invalid.active_setting_group = 5U;
        QSet<QString> invalidSet;
        CHECK(arstack::iedsim::settingControlManifestLine(
            invalid, QStringLiteral("SGIED"), invalidSet).isEmpty());

        std::cout << "SETTING_CONTROL_MANIFEST_PASS exact=pass duplicate=pass invalid=omitted\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SETTING_CONTROL_MANIFEST_FAIL " << error.what() << '\n';
        return 1;
    }
}
