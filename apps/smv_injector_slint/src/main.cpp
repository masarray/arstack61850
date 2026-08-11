#include "app-window.h"

#include <iostream>
#include <string_view>

int main()
{
    auto ui = AppWindow::create();

    ui->on_action_requested([](const slint::SharedString& action) {
        std::cout << "[smv-shell] action=" << std::string_view(action) << '\n';
    });

    ui->on_live_apply_requested([](const slint::SharedString& channel,
                                   const slint::SharedString& field,
                                   const slint::SharedString& value) {
        std::cout << "[smv-shell] live-apply channel=" << std::string_view(channel)
                  << " field=" << std::string_view(field)
                  << " value=" << std::string_view(value) << '\n';
    });

    ui->run();
    return 0;
}
