#include "serial_device.hpp"

#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::string windows_error(std::string operation)
{
    const auto code = GetLastError();
    return operation + " failed (Windows error " + std::to_string(code) + ")";
}

HANDLE native_handle(void* handle)
{
    return static_cast<HANDLE>(handle);
}

} // namespace
#endif

SerialDevice::~SerialDevice()
{
    close();
}

bool SerialDevice::open(std::string port_name, std::uint32_t baud_rate)
{
    std::scoped_lock lock(mutex_);
    close_unlocked();

#ifdef _WIN32
    port_name.erase(
        std::remove_if(port_name.begin(), port_name.end(), [](unsigned char c) { return c == ' ' || c == '\t'; }),
        port_name.end());
    if (port_name.empty()) {
        last_error_ = "Enter a serial port such as COM3";
        return false;
    }

    if (!port_name.starts_with("\\\\.\\")) {
        port_name = "\\\\.\\" + port_name;
    }

    const auto handle = CreateFileA(port_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        last_error_ = windows_error("Opening " + port_name);
        return false;
    }

    DCB settings{};
    settings.DCBlength = sizeof(settings);
    if (!GetCommState(handle, &settings)) {
        last_error_ = windows_error("Reading serial settings");
        CloseHandle(handle);
        return false;
    }

    settings.BaudRate = baud_rate;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fDtrControl = DTR_CONTROL_ENABLE;
    settings.fRtsControl = RTS_CONTROL_ENABLE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;
    settings.fOutX = FALSE;
    settings.fInX = FALSE;

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 250;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommState(handle, &settings) || !SetCommTimeouts(handle, &timeouts)
        || !SetupComm(handle, 4096, 4096)) {
        last_error_ = windows_error("Configuring serial port");
        CloseHandle(handle);
        return false;
    }

    PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    handle_ = handle;
    last_error_.clear();
    return true;
#else
    (void)port_name;
    (void)baud_rate;
    last_error_ = "Native serial control is currently available on Windows";
    return false;
#endif
}

void SerialDevice::close()
{
    std::scoped_lock lock(mutex_);
    close_unlocked();
}

void SerialDevice::close_unlocked()
{
#ifdef _WIN32
    if (handle_ != nullptr && native_handle(handle_) != INVALID_HANDLE_VALUE) {
        CloseHandle(native_handle(handle_));
    }
#endif
    handle_ = nullptr;
}

bool SerialDevice::is_open() const
{
    std::scoped_lock lock(mutex_);
    return handle_ != nullptr;
}

bool SerialDevice::send_line(std::string_view command)
{
    std::scoped_lock lock(mutex_);
#ifdef _WIN32
    if (handle_ == nullptr) {
        last_error_ = "No serial device is connected";
        return false;
    }

    std::string payload(command);
    if (payload.empty() || payload.back() != '\n') {
        payload += "\r\n";
    }

    std::size_t written_total = 0;
    while (written_total < payload.size()) {
        DWORD written = 0;
        const auto remaining = static_cast<DWORD>(payload.size() - written_total);
        if (!WriteFile(native_handle(handle_), payload.data() + written_total, remaining, &written, nullptr)) {
            last_error_ = windows_error("Writing serial command");
            return false;
        }
        if (written == 0) {
            last_error_ = "Serial write completed without sending data";
            return false;
        }
        written_total += written;
    }

    last_error_.clear();
    return true;
#else
    (void)command;
    last_error_ = "Native serial control is currently available on Windows";
    return false;
#endif
}

std::string SerialDevice::last_error() const
{
    std::scoped_lock lock(mutex_);
    return last_error_;
}
