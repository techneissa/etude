// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli.hpp"
#include "window_options.hpp"

#include <SDL3/SDL_main.h>

#include <chrono>
#include <optional>

namespace etude::app {

std::chrono::steady_clock::time_point g_process_start;

} // namespace etude::app

int main(int argc, char* argv[]) {
    if (const std::optional<int> done = etude::app::run_tool(argc, argv)) {
        return *done;
    }
    etude::app::g_process_start = std::chrono::steady_clock::now();
    etude::app::CommandLine cl;
    if (const std::optional<int> done = etude::app::parse_command_line(argc, argv, cl)) {
        return *done;
    }
    return etude::app::run_command(cl);
}
