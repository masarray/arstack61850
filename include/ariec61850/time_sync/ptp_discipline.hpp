// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PTP-P2 is header-only so the same portable clock-discipline implementation
// is consumed by the core library, desktop tools and ESP32-P4 adapter without
// target-specific linkage or duplicated servo logic.
#include "ariec61850/time_sync/ptp_discipline_impl.hpp"
