// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PTP-P2 is header-only so the exact same math, receiver correlation and servo
// policy are consumed by portable tools/tests and the ESP32-P4 adapter.
#include "ariec61850/time_sync/ptp_discipline_types.hpp"
#include "ariec61850/time_sync/ptp_clock_discipline.hpp"
#include "ariec61850/time_sync/ptp_time_receiver.hpp"
