#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

class SerialDevice final {
public:
    SerialDevice() = default;
    ~SerialDevice();

    SerialDevice(const SerialDevice&) = delete;
    SerialDevice& operator=(const SerialDevice&) = delete;

    bool open(std::string port_name, std::uint32_t baud_rate = 115200);
    void close();
    [[nodiscard]] bool is_open() const;
    bool send_line(std::string_view command);
    [[nodiscard]] std::string last_error() const;

private:
    void close_unlocked();

    mutable std::mutex mutex_;
    void* handle_ = nullptr;
    std::string last_error_;
};
