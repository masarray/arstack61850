// SPDX-License-Identifier: GPL-3.0-or-later

#include <csignal>

namespace {

#if !defined(_WIN32)
struct PosixSigpipeGuard final {
    PosixSigpipeGuard() noexcept {
        std::signal(SIGPIPE, SIG_IGN);
    }
};

[[maybe_unused]] const PosixSigpipeGuard kPosixSigpipeGuard{};
#endif

} // namespace
