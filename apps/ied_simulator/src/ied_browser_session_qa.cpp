// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedBrowserSessionController.hpp"

#include <QCoreApplication>

#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    MmsClientController client;
    MmsReportController reports;
    MmsFileSettingsController utilities;
    IedBrowserSessionController browser;

    browser.setClient(&client);
    browser.setReports(&reports);
    browser.setUtilities(&utilities);

    browser.setHost(QStringLiteral("192.0.2.40"));
    browser.setPort(8102);
    browser.setTrustedSclPath(QStringLiteral("/tmp/browser-context.cid"));

    if (browser.host() != QStringLiteral("192.0.2.40") || browser.port() != 8102 ||
        browser.endpoint() != QStringLiteral("192.0.2.40:8102") ||
        client.host() != browser.host() || reports.host() != browser.host() ||
        utilities.host() != browser.host() || client.port() != browser.port() ||
        reports.port() != browser.port() || utilities.port() != browser.port() ||
        client.trustedSclPath() != browser.trustedSclPath()) {
        std::cerr << "Browser endpoint/trusted-SCL propagation failed.\n";
        return 2;
    }

    browser.setHost(QStringLiteral("[2001:db8::40]"));
    browser.setPort(102);
    if (browser.host() != QStringLiteral("2001:db8::40") ||
        browser.endpoint() != QStringLiteral("[2001:db8::40]:102") ||
        client.host() != browser.host() || reports.host() != browser.host() ||
        utilities.host() != browser.host()) {
        std::cerr << "Browser IPv6 endpoint normalization/propagation failed.\n";
        return 3;
    }

    browser.setPort(0);
    if (browser.port() != 102 || browser.lastError().isEmpty()) {
        std::cerr << "Invalid Browser port did not fail closed.\n";
        return 4;
    }

    browser.setHost(QStringLiteral("invalid host"));
    if (browser.host() != QStringLiteral("2001:db8::40") || browser.lastError().isEmpty()) {
        std::cerr << "Invalid Browser host did not fail closed.\n";
        return 5;
    }

    browser.setHost(QStringLiteral("relay.local"));
    if (browser.host() != QStringLiteral("relay.local") || !browser.lastError().isEmpty() ||
        client.host() != QStringLiteral("relay.local") ||
        reports.host() != QStringLiteral("relay.local") ||
        utilities.host() != QStringLiteral("relay.local")) {
        std::cerr << "Browser recovery after invalid configuration failed.\n";
        return 6;
    }

    browser.disconnectFromIed();
    if (browser.connected() || client.connected() || reports.connected() || utilities.connected()) {
        std::cerr << "Coordinated Browser disconnect contract failed.\n";
        return 7;
    }

    std::cout << "IED_BROWSER_SESSION_PASS"
              << " endpoint=relay.local:102"
              << " propagation=pass"
              << " trusted_scl=pass"
              << " invalid_endpoint=fail_closed"
              << " disconnect=coordinated\n";
    return 0;
}
