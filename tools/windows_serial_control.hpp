// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ar::iec61850::tools {

class WindowsSerialControlPort final {
public:
    WindowsSerialControlPort() = default;
    ~WindowsSerialControlPort() { close(); }

    WindowsSerialControlPort(const WindowsSerialControlPort&) = delete;
    WindowsSerialControlPort& operator=(const WindowsSerialControlPort&) = delete;
    WindowsSerialControlPort(WindowsSerialControlPort&&) = delete;
    WindowsSerialControlPort& operator=(WindowsSerialControlPort&&) = delete;

    [[nodiscard]] bool open(const std::string_view device) {
        close();
#ifdef _WIN32
        if (device.empty()) {
            error_ = "Serial device is empty.";
            return false;
        }

        std::string path{device};
        if (!path.starts_with("\\\\.\\")) {
            path = "\\\\.\\" + path;
        }

        handle_ = CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const auto code = GetLastError();
            handle_ = nullptr;
            error_ = "CreateFile failed for " + std::string{device} +
                " (" + describe_windows_error(code) + ").";
            return false;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (GetCommState(handle_, &dcb) == 0) {
            set_last_error("GetCommState failed");
            close();
            return false;
        }
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8U;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        if (SetCommState(handle_, &dcb) == 0) {
            set_last_error("SetCommState failed");
            close();
            return false;
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0U;
        timeouts.ReadTotalTimeoutConstant = 25U;
        timeouts.WriteTotalTimeoutMultiplier = 0U;
        timeouts.WriteTotalTimeoutConstant = 1'000U;
        if (SetCommTimeouts(handle_, &timeouts) == 0) {
            set_last_error("SetCommTimeouts failed");
            close();
            return false;
        }

        (void)PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
        device_ = std::string{device};
        error_.clear();
        return true;
#else
        (void)device;
        error_ = "Serial injector control is available only on Windows.";
        return false;
#endif
    }

    void close() noexcept {
#ifdef _WIN32
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#endif
        device_.clear();
    }

    [[nodiscard]] bool opened() const noexcept {
#ifdef _WIN32
        return handle_ != nullptr;
#else
        return false;
#endif
    }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& device() const noexcept { return device_; }

    [[nodiscard]] bool write_line(const std::string_view line) {
#ifdef _WIN32
        if (!opened()) {
            error_ = "Serial port is not open.";
            return false;
        }

        std::string framed{line};
        framed.push_back('\n');
        std::size_t offset = 0U;
        while (offset < framed.size()) {
            DWORD written = 0U;
            const auto remaining = framed.size() - offset;
            const auto chunk = remaining > static_cast<std::size_t>(MAXDWORD)
                ? MAXDWORD
                : static_cast<DWORD>(remaining);
            if (WriteFile(
                    handle_,
                    framed.data() + offset,
                    chunk,
                    &written,
                    nullptr) == 0) {
                set_last_error("WriteFile failed");
                return false;
            }
            if (written == 0U) {
                error_ = "Serial write made no progress.";
                return false;
            }
            offset += static_cast<std::size_t>(written);
        }
        return true;
#else
        (void)line;
        error_ = "Serial injector control is available only on Windows.";
        return false;
#endif
    }

    [[nodiscard]] bool read_arctrl_line(
        std::string& response,
        const std::uint32_t timeout_ms = 3'000U) {
        response.clear();
#ifdef _WIN32
        if (!opened()) {
            error_ = "Serial port is not open.";
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds{timeout_ms};
        std::string line;
        line.reserve(512U);
        while (std::chrono::steady_clock::now() < deadline) {
            char buffer[128]{};
            DWORD received = 0U;
            if (ReadFile(
                    handle_,
                    buffer,
                    static_cast<DWORD>(sizeof(buffer)),
                    &received,
                    nullptr) == 0) {
                set_last_error("ReadFile failed");
                return false;
            }

            if (received == 0U) {
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
                continue;
            }

            for (DWORD index = 0U; index < received; ++index) {
                const auto character = buffer[index];
                if (character == '\r') {
                    continue;
                }
                if (character == '\n') {
                    if (line.starts_with("ARCTRL ")) {
                        response = line.substr(7U);
                        error_.clear();
                        return true;
                    }
                    line.clear();
                    continue;
                }
                if (line.size() < 4'096U) {
                    line.push_back(character);
                } else {
                    line.clear();
                }
            }
        }

        error_ = "Timed out waiting for ARCTRL response from " + device_ +
            ". Verify that the injector firmware is running and that this COM port is the board UART control port.";
        return false;
#else
        (void)timeout_ms;
        error_ = "Serial injector control is available only on Windows.";
        return false;
#endif
    }

private:
#ifdef _WIN32
    [[nodiscard]] static std::string describe_windows_error(const DWORD code) {
        switch (code) {
        case ERROR_FILE_NOT_FOUND:
            return "Windows error 2: COM port not found; verify the current port number";
        case ERROR_ACCESS_DENIED:
            return "Windows error 5: access denied; close any serial monitor or other process using the port";
        case ERROR_SEM_TIMEOUT:
            return "Windows error 121: semaphore timeout; reconnect the USB-to-UART cable/device, verify the driver, and confirm the COM port still enumerates";
        default:
            return "Windows error " + std::to_string(code);
        }
    }

    void set_last_error(const std::string_view prefix) {
        error_ = std::string{prefix} + " (" + describe_windows_error(GetLastError()) + ").";
    }

    HANDLE handle_{};
#endif
    std::string error_;
    std::string device_;
};

} // namespace ar::iec61850::tools
