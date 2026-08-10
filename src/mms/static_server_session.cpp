// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_server_session.hpp"

#include <cstring>

namespace ar::iec61850::mms {

MmsStaticServerSessionResult MmsStaticServerSession::make_result(
    const MmsStaticServerSessionStatus status,
    const MmsStaticConnectionStatus connection_status,
    const std::size_t bytes_received,
    const std::size_t bytes_sent) const noexcept {
    return {
        status,
        connection_status,
        bytes_received,
        bytes_sent,
        receive_size_,
        response_size_ - response_offset_};
}

void MmsStaticServerSession::consume_input(const std::size_t consumed) noexcept {
    if (consumed == 0U || consumed > receive_size_) {
        return;
    }
    const auto remaining = receive_size_ - consumed;
    if (remaining != 0U) {
        std::memmove(
            buffers_.receive.data(),
            buffers_.receive.data() + consumed,
            remaining);
    }
    receive_size_ = remaining;
}

void MmsStaticServerSession::reset() noexcept {
    receive_size_ = 0U;
    response_offset_ = 0U;
    response_size_ = 0U;
    terminal_ = false;
    runtime_.reset();
}

MmsStaticServerSessionResult MmsStaticServerSession::poll_once() noexcept {
    if (!buffers_.valid() || stream_.send_callback == nullptr ||
        stream_.receive_callback == nullptr) {
        terminal_ = true;
        return make_result(
            MmsStaticServerSessionStatus::invalid_configuration,
            MmsStaticConnectionStatus::backend_failure);
    }
    if (terminal_) {
        return make_result(
            MmsStaticServerSessionStatus::protocol_error,
            MmsStaticConnectionStatus::closed);
    }

    if (response_offset_ < response_size_) {
        const auto remaining = std::span<const std::uint8_t>{buffers_.response}
            .subspan(response_offset_, response_size_ - response_offset_);
        const auto sent = stream_.send(remaining);
        if (sent.status == embedded::IoStatus::would_block) {
            return make_result(
                MmsStaticServerSessionStatus::would_block,
                MmsStaticConnectionStatus::response_ready);
        }
        if (sent.status == embedded::IoStatus::timeout) {
            return make_result(
                MmsStaticServerSessionStatus::timed_out,
                MmsStaticConnectionStatus::response_ready);
        }
        if (sent.status == embedded::IoStatus::closed) {
            terminal_ = true;
            return make_result(
                MmsStaticServerSessionStatus::peer_closed,
                MmsStaticConnectionStatus::closed);
        }
        if (!sent.success() || sent.transferred == 0U ||
            sent.transferred > remaining.size()) {
            terminal_ = true;
            return make_result(
                MmsStaticServerSessionStatus::transport_error,
                MmsStaticConnectionStatus::backend_failure);
        }

        response_offset_ += sent.transferred;
        if (response_offset_ == response_size_) {
            response_offset_ = 0U;
            response_size_ = 0U;
        }
        return make_result(
            MmsStaticServerSessionStatus::progressed,
            MmsStaticConnectionStatus::response_ready,
            0U,
            sent.transferred);
    }

    if (receive_size_ != 0U) {
        const auto connection = runtime_.process_tcp_window(
            std::span<const std::uint8_t>{buffers_.receive}.first(receive_size_),
            buffers_.response,
            buffers_.workspace);

        switch (connection.status) {
        case MmsStaticConnectionStatus::response_ready:
            consume_input(connection.consumed_bytes);
            response_offset_ = 0U;
            response_size_ = connection.bytes_written;
            return make_result(
                MmsStaticServerSessionStatus::response_pending,
                connection.status);
        case MmsStaticConnectionStatus::consumed_no_response:
            consume_input(connection.consumed_bytes);
            return make_result(
                MmsStaticServerSessionStatus::progressed,
                connection.status);
        case MmsStaticConnectionStatus::application_rejected:
            consume_input(connection.consumed_bytes);
            return make_result(
                MmsStaticServerSessionStatus::application_rejected,
                connection.status);
        case MmsStaticConnectionStatus::closed:
            consume_input(connection.consumed_bytes);
            terminal_ = true;
            return make_result(
                MmsStaticServerSessionStatus::peer_closed,
                connection.status);
        case MmsStaticConnectionStatus::need_more:
            break;
        case MmsStaticConnectionStatus::malformed_transport:
        case MmsStaticConnectionStatus::protocol_violation:
        case MmsStaticConnectionStatus::response_buffer_too_small:
        case MmsStaticConnectionStatus::workspace_too_small:
        case MmsStaticConnectionStatus::backend_failure:
            terminal_ = true;
            return make_result(
                MmsStaticServerSessionStatus::protocol_error,
                connection.status);
        }
    }

    if (receive_size_ == buffers_.receive.size()) {
        terminal_ = true;
        return make_result(
            MmsStaticServerSessionStatus::receive_buffer_full,
            MmsStaticConnectionStatus::need_more);
    }

    const auto destination = buffers_.receive.subspan(receive_size_);
    const auto received = stream_.receive(destination);
    if (received.status == embedded::IoStatus::would_block) {
        return make_result(
            MmsStaticServerSessionStatus::would_block,
            MmsStaticConnectionStatus::need_more);
    }
    if (received.status == embedded::IoStatus::timeout) {
        return make_result(
            MmsStaticServerSessionStatus::timed_out,
            MmsStaticConnectionStatus::need_more);
    }
    if (received.status == embedded::IoStatus::closed) {
        terminal_ = true;
        return make_result(
            MmsStaticServerSessionStatus::peer_closed,
            MmsStaticConnectionStatus::closed);
    }
    if (!received.success() || received.transferred == 0U ||
        received.transferred > destination.size()) {
        terminal_ = true;
        return make_result(
            MmsStaticServerSessionStatus::transport_error,
            MmsStaticConnectionStatus::backend_failure);
    }

    receive_size_ += received.transferred;
    return make_result(
        MmsStaticServerSessionStatus::progressed,
        MmsStaticConnectionStatus::need_more,
        received.transferred,
        0U);
}

} // namespace ar::iec61850::mms
