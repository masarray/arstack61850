#include "app-window.h"
#include "serial_device.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr double pi = 3.14159265358979323846;

struct SignalChannel {
    std::string id;
    bool current;
    bool enabled;
    double magnitude;
    double phase_degrees;
};

using SignalState = std::array<SignalChannel, 8>;

SignalState default_signal_state()
{
    return {{{"IA", true, true, 1.0, 0.0},
             {"IB", true, true, 1.0, -120.0},
             {"IC", true, true, 1.0, 120.0},
             {"IN", true, false, 0.0, 0.0},
             {"UA", false, true, 57.74, 0.0},
             {"UB", false, true, 57.74, -120.0},
             {"UC", false, true, 57.74, 120.0},
             {"UN", false, false, 0.0, 0.0}}};
}

std::optional<std::size_t> channel_index(std::string_view id)
{
    constexpr std::array<std::string_view, 8> ids{"IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN"};
    const auto match = std::find(ids.begin(), ids.end(), id);
    if (match == ids.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(ids.begin(), match));
}

double group_maximum(const SignalState& state, bool current)
{
    double maximum = 0.0;
    for (const auto& channel : state) {
        if (channel.current == current && channel.enabled && channel.id.back() != 'N') {
            maximum = std::max(maximum, channel.magnitude);
        }
    }
    return maximum;
}

std::string waveform_path(const SignalChannel& channel, double maximum, double frequency_hz)
{
    constexpr int samples = 96;
    constexpr double width = 200.0;
    const double center = channel.current ? 75.0 : 25.0;
    const double amplitude = channel.enabled && maximum > 0.0 ? 17.5 * channel.magnitude / maximum : 0.0;
    const double phase = channel.phase_degrees * pi / 180.0;

    std::ostringstream path;
    path.imbue(std::locale::classic());
    path << std::fixed << std::setprecision(2);
    for (int sample = 0; sample <= samples; ++sample) {
        const double ratio = static_cast<double>(sample) / samples;
        const double seconds = ratio * 0.040;
        const double x = width * ratio;
        const double y = center - amplitude * std::sin((2.0 * pi * frequency_hz * seconds) + phase);
        path << (sample == 0 ? "M " : " L ") << x << ' ' << y;
    }
    return path.str();
}

std::string phasor_path(const SignalChannel& channel, double maximum)
{
    if (!channel.enabled || maximum <= 0.0 || channel.magnitude <= 0.0) {
        return "M 50 50";
    }

    const double angle = channel.phase_degrees * pi / 180.0;
    const double radius = (channel.current ? 37.0 : 32.0) * std::min(1.0, channel.magnitude / maximum);
    const double x = 50.0 + std::cos(angle) * radius;
    const double y = 50.0 - std::sin(angle) * radius;
    const double head = 3.2;
    const double left_x = x - std::cos(angle - 0.55) * head;
    const double left_y = y + std::sin(angle - 0.55) * head;
    const double right_x = x - std::cos(angle + 0.55) * head;
    const double right_y = y + std::sin(angle + 0.55) * head;

    std::ostringstream path;
    path.imbue(std::locale::classic());
    path << std::fixed << std::setprecision(2)
         << "M 50 50 L " << x << ' ' << y
         << " M " << left_x << ' ' << left_y << " L " << x << ' ' << y
         << " L " << right_x << ' ' << right_y;
    return path.str();
}

void sync_visuals(const slint::ComponentHandle<AppWindow>& ui, const SignalState& state, double frequency_hz)
{
    const auto current_max = group_maximum(state, true);
    const auto voltage_max = group_maximum(state, false);

    ui->set_ia_wave_path(waveform_path(state[0], current_max, frequency_hz).c_str());
    ui->set_ib_wave_path(waveform_path(state[1], current_max, frequency_hz).c_str());
    ui->set_ic_wave_path(waveform_path(state[2], current_max, frequency_hz).c_str());
    ui->set_ua_wave_path(waveform_path(state[4], voltage_max, frequency_hz).c_str());
    ui->set_ub_wave_path(waveform_path(state[5], voltage_max, frequency_hz).c_str());
    ui->set_uc_wave_path(waveform_path(state[6], voltage_max, frequency_hz).c_str());

    ui->set_ia_phasor_path(phasor_path(state[0], current_max).c_str());
    ui->set_ib_phasor_path(phasor_path(state[1], current_max).c_str());
    ui->set_ic_phasor_path(phasor_path(state[2], current_max).c_str());
    ui->set_ua_phasor_path(phasor_path(state[4], voltage_max).c_str());
    ui->set_ub_phasor_path(phasor_path(state[5], voltage_max).c_str());
    ui->set_uc_phasor_path(phasor_path(state[6], voltage_max).c_str());
}

void sync_editor_values(const slint::ComponentHandle<AppWindow>& ui, const SignalState& state)
{
    ui->set_ia_enabled(state[0].enabled);
    ui->set_ib_enabled(state[1].enabled);
    ui->set_ic_enabled(state[2].enabled);
    ui->set_in_enabled(state[3].enabled);
    ui->set_ua_enabled(state[4].enabled);
    ui->set_ub_enabled(state[5].enabled);
    ui->set_uc_enabled(state[6].enabled);
    ui->set_un_enabled(state[7].enabled);

    ui->set_ia_magnitude(static_cast<float>(state[0].magnitude));
    ui->set_ib_magnitude(static_cast<float>(state[1].magnitude));
    ui->set_ic_magnitude(static_cast<float>(state[2].magnitude));
    ui->set_in_magnitude(static_cast<float>(state[3].magnitude));
    ui->set_ua_magnitude(static_cast<float>(state[4].magnitude));
    ui->set_ub_magnitude(static_cast<float>(state[5].magnitude));
    ui->set_uc_magnitude(static_cast<float>(state[6].magnitude));
    ui->set_un_magnitude(static_cast<float>(state[7].magnitude));

    ui->set_ia_phase(static_cast<float>(state[0].phase_degrees));
    ui->set_ib_phase(static_cast<float>(state[1].phase_degrees));
    ui->set_ic_phase(static_cast<float>(state[2].phase_degrees));
    ui->set_in_phase(static_cast<float>(state[3].phase_degrees));
    ui->set_ua_phase(static_cast<float>(state[4].phase_degrees));
    ui->set_ub_phase(static_cast<float>(state[5].phase_degrees));
    ui->set_uc_phase(static_cast<float>(state[6].phase_degrees));
    ui->set_un_phase(static_cast<float>(state[7].phase_degrees));
}

bool valid_signal_value(const SignalChannel& channel, double magnitude, double phase)
{
    const double maximum = channel.current ? 10000.0 : 1000000.0;
    return std::isfinite(magnitude) && std::isfinite(phase) && magnitude >= 0.0 && magnitude <= maximum
        && phase >= -360.0 && phase <= 360.0;
}

std::string signal_command(const SignalChannel& channel)
{
    const auto scale = channel.current ? 1000.0 : 100.0;
    const auto counts = std::llround(channel.magnitude * scale);
    const auto phase_mdeg = std::llround(channel.phase_degrees * 1000.0);
    return "SET " + channel.id + " " + std::to_string(counts) + " " + std::to_string(phase_mdeg) + " 0";
}

void record_command(const slint::ComponentHandle<AppWindow>& ui, std::string_view command)
{
    ui->set_last_command(slint::SharedString(command));
    ui->set_tx_sequence(ui->get_tx_sequence() + 1);
    ui->set_status_error(false);
}

bool transmit(const slint::ComponentHandle<AppWindow>& ui, SerialDevice& device, std::string_view command)
{
    if (!device.send_line(command)) {
        ui->set_status_message(("Transmit failed: " + device.last_error()).c_str());
        ui->set_status_error(true);
        return false;
    }
    record_command(ui, command);
    return true;
}

void set_balanced_state(SignalState& state)
{
    state = default_signal_state();
}

} // namespace

int main()
{
    auto ui = AppWindow::create();
    auto state = default_signal_state();
    double frequency_hz = 50.0;
    SerialDevice device;

    sync_editor_values(ui, state);
    sync_visuals(ui, state, frequency_hz);

    const auto ui_weak = slint::ComponentWeakHandle(ui);

    ui->on_action_requested([ui_weak, &device, &state, &frequency_hz](const slint::SharedString& action_value) {
        const auto locked = ui_weak.lock();
        if (!locked) {
            return;
        }
        const auto& window = *locked;
        const std::string action{std::string_view(action_value)};

        if (action == "connect-device") {
            const std::string port(std::string_view(window->get_serial_port()));
            if (!device.open(port)) {
                window->set_connected(false);
                window->set_status_message(("Connection failed: " + device.last_error()).c_str());
                window->set_status_error(true);
                return;
            }
            window->set_connected(true);
            window->set_running(false);
            window->set_status_message(("Connected to " + port + " at 115200 baud").c_str());
            window->set_status_error(false);
            transmit(window, device, "SHOW");
            return;
        }

        if (action == "disconnect-device") {
            device.close();
            window->set_connected(false);
            window->set_running(false);
            window->set_status_message("Device disconnected. Signal values remain staged.");
            window->set_status_error(false);
            return;
        }

        if (action == "start-injection") {
            if (transmit(window, device, "START")) {
                window->set_run_elapsed_seconds(0);
                window->set_running(true);
                window->set_status_message("START transmitted. Publisher state set to running.");
            }
            return;
        }

        if (action == "stop-injection") {
            if (transmit(window, device, "STOP")) {
                window->set_running(false);
                window->set_status_message("STOP transmitted. Profile remains armed for the next run.");
            }
            return;
        }

        if (action == "prefault" || action == "manual") {
            set_balanced_state(state);
            frequency_hz = 50.0;
            window->set_frequency_hz(50.0F);
            sync_editor_values(window, state);
            sync_visuals(window, state, frequency_hz);
            window->set_status_message("Balanced Quick Manual values restored and validated.");
            window->set_status_error(false);
            return;
        }

        if (action == "zero-output") {
            for (auto& channel : state) {
                channel.magnitude = 0.0;
            }
            sync_editor_values(window, state);
            sync_visuals(window, state, frequency_hz);
            if (window->get_connected()) {
                transmit(window, device, "ZERO");
            }
            window->set_status_message("All analog outputs set to zero; waveform preview updated.");
            window->set_status_error(false);
            return;
        }

        if (action == "apply-all") {
            if (!window->get_connected()) {
                window->set_status_message("Apply all staged values: connect the ESP32-P4 first.");
                window->set_status_error(true);
                return;
            }
            for (const auto& channel : state) {
                if (channel.enabled && !transmit(window, device, signal_command(channel))) {
                    return;
                }
            }
            const auto frequency_command = "FREQ " + std::to_string(std::llround(frequency_hz * 1000.0));
            if (transmit(window, device, frequency_command)) {
                window->set_status_message("All enabled analog values validated and transmitted.");
                window->set_status_error(false);
            }
            return;
        }

        if (action == "import-scl") {
            window->set_status_message("SCL import is the next native controller integration seam.");
            window->set_status_error(false);
            return;
        }

        if (action == "config" || action == "ramp" || action == "sequence") {
            window->set_status_message("Quick Manual is the implemented fast workflow; advanced module integration remains staged.");
            window->set_status_error(false);
        }
    });

    ui->on_cell_commit_requested(
        [ui_weak, &device, &state, &frequency_hz](const slint::SharedString& channel_value,
                                                  int column,
                                                  float requested_value,
                                                  bool force_commit) -> bool {
            (void)force_commit;
            const auto locked = ui_weak.lock();
            if (!locked) {
                return false;
            }
            const auto& window = *locked;
            const std::string channel_id{std::string_view(channel_value)};
            const auto index = channel_index(channel_id);
            if (!index) {
                window->set_status_message("Rejected signal edit: unknown channel.");
                window->set_status_error(true);
                return false;
            }

            auto& channel = state[*index];
            if (column == 2) {
                if (!std::isfinite(requested_value) || requested_value < 1.0F || requested_value > 1000.0F) {
                    window->set_status_message("Invalid frequency. Enter a value from 1 to 1000 Hz.");
                    window->set_status_error(true);
                    window->set_frequency_hz(static_cast<float>(frequency_hz));
                    return false;
                }
                frequency_hz = requested_value;
                window->set_frequency_hz(requested_value);
                sync_visuals(window, state, frequency_hz);
            } else {
                const double magnitude = column == 0 ? requested_value : channel.magnitude;
                const double phase = column == 1 ? requested_value : channel.phase_degrees;
                if (!valid_signal_value(channel, magnitude, phase)) {
                    const auto range = column == 0
                        ? (channel.current ? "0 to 10000 A RMS" : "0 to 1000000 V RMS")
                        : "-360 to 360 degrees";
                    window->set_status_message(("Invalid " + channel.id + " value. Allowed range: " + range + ".").c_str());
                    window->set_status_error(true);
                    sync_editor_values(window, state);
                    return false;
                }
                channel.magnitude = magnitude;
                channel.phase_degrees = phase;
                sync_visuals(window, state, frequency_hz);
            }

            if (!window->get_connected()) {
                window->set_status_message((channel.id + " validated; preview updated and staged offline.").c_str());
                window->set_status_error(false);
                return true;
            }

            if (!window->get_live_apply_enabled()) {
                window->set_status_message((channel.id + " validated and staged. Use Apply all to transmit.").c_str());
                window->set_status_error(false);
                return true;
            }

            const auto command = column == 2
                ? "FREQ " + std::to_string(std::llround(frequency_hz * 1000.0))
                : signal_command(channel);
            if (transmit(window, device, command)) {
                window->set_status_message((channel.id + " validated and live-applied to the next coherent generation.").c_str());
            }
            return true;
        });

    ui->on_channel_enabled_requested([ui_weak, &device, &state, &frequency_hz](const slint::SharedString& channel_value,
                                                                               bool enabled) {
        const auto locked = ui_weak.lock();
        if (!locked) {
            return;
        }
        const auto& window = *locked;
        const std::string channel_id{std::string_view(channel_value)};
        const auto index = channel_index(channel_id);
        if (!index) {
            window->set_status_message("Rejected channel toggle: unknown channel.");
            window->set_status_error(true);
            return;
        }
        state[*index].enabled = enabled;
        sync_visuals(window, state, frequency_hz);
        const auto command = "ENABLE " + channel_id + " " + (enabled ? "1" : "0");
        if (window->get_connected() && window->get_live_apply_enabled()) {
            transmit(window, device, command);
        }
        window->set_status_message((channel_id + (enabled ? " enabled." : " disabled.")).c_str());
        window->set_status_error(false);
    });

    ui->run();
    return 0;
}
