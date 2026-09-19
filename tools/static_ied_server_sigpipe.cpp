// SPDX-License-Identifier: GPL-3.0-or-later

#include <csignal>
#include <cstring>
#include <iostream>
#include <streambuf>

namespace {

// std::osyncstream makes concurrent event records atomic, but emitting a
// completed record does not necessarily flush the wrapped stdout pipe. The Qt
// host consumes these records as a control channel (not just diagnostic text),
// so a server_ready line must become visible immediately. Keep the buffering
// policy at the desktop adapter boundary rather than leaking flush calls into
// the protocol/session core.
class LineFlushingStreamBuffer final : public std::streambuf {
public:
    explicit LineFlushingStreamBuffer(std::streambuf* wrapped) noexcept
        : wrapped_{wrapped} {}

protected:
    int_type overflow(const int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) {
            return traits_type::not_eof(ch);
        }
        if (wrapped_ == nullptr ||
            traits_type::eq_int_type(
                wrapped_->sputc(traits_type::to_char_type(ch)),
                traits_type::eof())) {
            return traits_type::eof();
        }
        if (traits_type::to_char_type(ch) == '\n' && wrapped_->pubsync() != 0) {
            return traits_type::eof();
        }
        return ch;
    }

    std::streamsize xsputn(const char* const data, const std::streamsize count) override {
        if (wrapped_ == nullptr || data == nullptr || count <= 0) return 0;
        const auto written = wrapped_->sputn(data, count);
        if (written > 0 &&
            std::memchr(data, '\n', static_cast<std::size_t>(written)) != nullptr) {
            static_cast<void>(wrapped_->pubsync());
        }
        return written;
    }

    int sync() override {
        return wrapped_ == nullptr ? -1 : wrapped_->pubsync();
    }

private:
    std::streambuf* wrapped_{};
};

struct DesktopStreamGuard final {
    DesktopStreamGuard() noexcept
        : cout_buffer_{std::cout.rdbuf()},
          cerr_buffer_{std::cerr.rdbuf()},
          original_cout_{std::cout.rdbuf(&cout_buffer_)},
          original_cerr_{std::cerr.rdbuf(&cerr_buffer_)} {}

    ~DesktopStreamGuard() {
        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf(original_cout_);
        std::cerr.rdbuf(original_cerr_);
    }

    LineFlushingStreamBuffer cout_buffer_;
    LineFlushingStreamBuffer cerr_buffer_;
    std::streambuf* original_cout_{};
    std::streambuf* original_cerr_{};
};

[[maybe_unused]] DesktopStreamGuard kDesktopStreamGuard{};

#if !defined(_WIN32)
struct PosixSigpipeGuard final {
    PosixSigpipeGuard() noexcept {
        std::signal(SIGPIPE, SIG_IGN);
    }
};

[[maybe_unused]] const PosixSigpipeGuard kPosixSigpipeGuard{};
#endif

} // namespace