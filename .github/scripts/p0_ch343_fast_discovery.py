from pathlib import Path


def replace(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count} occurrence(s), found {actual}: {old[:120]!r}")
    p.write_text(text.replace(old, new, count), encoding="utf-8")


# Expose a pure metadata scorer so CI can lock the discovery policy without
# needing physical USB hardware. CH343 is only a transport hint; IDENTIFY stays
# authoritative for ARStack identity.
replace(
    "apps/arstack_studio/src/DeviceIoWorker.hpp",
    """    [[nodiscard]] static constexpr int heartbeatIntervalMs() noexcept { return 700; }\n\n    // S8B Windows ownership contract:""",
    """    [[nodiscard]] static constexpr int heartbeatIntervalMs() noexcept { return 700; }\n\n    // Fast discovery policy: USB bridge metadata may prioritize a port, but it\n    // never verifies device identity. The opened port must still answer the\n    // semantic IDENTIFY contract before Studio can become READY.\n    [[nodiscard]] static int portConfidenceForMetadata(\n        const bool hasVendorIdentifier,\n        const quint16 vendorIdentifier,\n        const bool hasProductIdentifier,\n        const quint16 productIdentifier,\n        const QString& description,\n        const QString& manufacturer,\n        const QString& serialNumber) {\n        int score = 0;\n        if (hasVendorIdentifier && vendorIdentifier == 0x303AU) score += 100;\n\n        const QString identity = QStringLiteral(\"%1 %2 %3\")\n            .arg(description, manufacturer, serialNumber).toLower();\n\n        // The current ARStack ESP32-P4 board exposes its console through a WCH\n        // CH343 bridge on Windows. Treat that bridge as a fast transport\n        // candidate, not as proof that the device is ARStack.\n        const bool saysCh343 = identity.contains(QStringLiteral(\"ch343\"));\n        const bool wchCh343 = hasVendorIdentifier && vendorIdentifier == 0x1A86U &&\n            hasProductIdentifier && productIdentifier == 0x55D3U;\n        if (wchCh343) score += 95;\n        else if (saysCh343) score += 85;\n\n        if (identity.contains(QStringLiteral(\"esp32-p4\"))) score += 90;\n        else if (identity.contains(QStringLiteral(\"esp32\"))) score += 65;\n        if (identity.contains(QStringLiteral(\"espressif\"))) score += 60;\n        if (identity.contains(QStringLiteral(\"usb jtag\")) ||\n            identity.contains(QStringLiteral(\"usb serial\")) ||\n            identity.contains(QStringLiteral(\"usb-enhanced-serial\"))) {\n            score += 15;\n        }\n\n        // Windows can expose many virtual Bluetooth COM ports. They are poor\n        // automatic injector candidates and must never outrank a physical USB\n        // bridge. Manual selection remains available.\n        if (identity.contains(QStringLiteral(\"bluetooth\"))) score -= 120;\n        return score;\n    }\n\n    // S8B Windows ownership contract:""",
)

replace(
    "apps/arstack_studio/src/DeviceIoWorker.cpp",
    """[[nodiscard]] int portConfidence(const QSerialPortInfo& info) {\n    int score = 0;\n    if (info.hasVendorIdentifier() && info.vendorIdentifier() == 0x303AU) score += 100;\n    const QString identity = QStringLiteral(\"%1 %2 %3\")\n        .arg(info.description(), info.manufacturer(), info.serialNumber()).toLower();\n    if (identity.contains(QStringLiteral(\"esp32-p4\"))) score += 90;\n    else if (identity.contains(QStringLiteral(\"esp32\"))) score += 65;\n    if (identity.contains(QStringLiteral(\"espressif\"))) score += 60;\n    if (identity.contains(QStringLiteral(\"usb jtag\")) ||\n        identity.contains(QStringLiteral(\"usb serial\"))) score += 15;\n    return score;\n}\n""",
    """[[nodiscard]] int portConfidence(const QSerialPortInfo& info) {\n    return DeviceIoWorker::portConfidenceForMetadata(\n        info.hasVendorIdentifier(),\n        info.hasVendorIdentifier() ? info.vendorIdentifier() : 0U,\n        info.hasProductIdentifier(),\n        info.hasProductIdentifier() ? info.productIdentifier() : 0U,\n        info.description(),\n        info.manufacturer(),\n        info.serialNumber());\n}\n""",
)

replace(
    "apps/arstack_studio/src/WindowsOwnershipHarness.cpp",
    """bool crossProcessOwnershipAndCrashRecovery() {\n""",
    """bool fastUsbDiscoveryPolicy() {\n    const int ch343Exact = DeviceIoWorker::portConfidenceForMetadata(\n        true, 0x1A86U, true, 0x55D3U,\n        QStringLiteral(\"USB-Enhanced-SERIAL CH343\"), {}, {});\n    const int ch343ByName = DeviceIoWorker::portConfidenceForMetadata(\n        false, 0U, false, 0U,\n        QStringLiteral(\"USB-Enhanced-SERIAL CH343\"), {}, {});\n    const int bluetooth = DeviceIoWorker::portConfidenceForMetadata(\n        false, 0U, false, 0U,\n        QStringLiteral(\"Standard Serial over Bluetooth link\"), {}, {});\n    const int generic = DeviceIoWorker::portConfidenceForMetadata(\n        false, 0U, false, 0U, QStringLiteral(\"USB Serial Port\"), {}, {});\n    const int espressif = DeviceIoWorker::portConfidenceForMetadata(\n        true, 0x303AU, false, 0U, QStringLiteral(\"USB JTAG/serial debug unit\"),\n        QStringLiteral(\"Espressif\"), {});\n\n    return ch343Exact >= 60 && ch343ByName >= 60 && espressif >= 60 &&\n        ch343Exact > generic && ch343ByName > generic &&\n        ch343Exact > bluetooth && bluetooth < generic;\n}\n\nbool crossProcessOwnershipAndCrashRecovery() {\n""",
)

replace(
    "apps/arstack_studio/src/WindowsOwnershipHarness.cpp",
    """    const bool serialPolicy = serialTransportLossPolicy();\n    const bool processOwnership = crossProcessOwnershipAndCrashRecovery();\n\n    qInfo().noquote()\n        << \"S8B Windows ownership:\"\n        << (serialPolicy ? \"PASS\" : \"FAIL\")\n        << \"· Permission/Resource/DeviceNotFound/Read/Write errors force transport loss\";\n""",
    """    const bool serialPolicy = serialTransportLossPolicy();\n    const bool fastDiscovery = fastUsbDiscoveryPolicy();\n    const bool processOwnership = crossProcessOwnershipAndCrashRecovery();\n\n    qInfo().noquote()\n        << \"S8B Windows ownership:\"\n        << (serialPolicy ? \"PASS\" : \"FAIL\")\n        << \"· Permission/Resource/DeviceNotFound/Read/Write errors force transport loss\";\n    qInfo().noquote()\n        << \"S8B Windows discovery:\"\n        << (fastDiscovery ? \"PASS\" : \"FAIL\")\n        << \"· CH343/Espressif USB outrank generic and Bluetooth COM ports; IDENTIFY remains authoritative\";\n""",
)

replace(
    "apps/arstack_studio/src/WindowsOwnershipHarness.cpp",
    """    if (!serialPolicy || !processOwnership) {\n        qCritical().noquote() << \"S8B Windows ownership integration harness: FAIL\";\n        return 22;\n    }\n\n    qInfo().noquote()\n        << \"S8B Windows ownership integration harness: PASS · OS process lock + crash recovery + serial transport-loss policy locked\";\n""",
    """    if (!serialPolicy || !fastDiscovery || !processOwnership) {\n        qCritical().noquote() << \"S8B Windows ownership/discovery integration harness: FAIL\";\n        return 22;\n    }\n\n    qInfo().noquote()\n        << \"S8B Windows ownership/discovery integration harness: PASS · fast CH343 discovery + OS process lock + crash recovery + serial transport-loss policy locked\";\n""",
)

print("P0 CH343 fast-discovery source patch applied")
