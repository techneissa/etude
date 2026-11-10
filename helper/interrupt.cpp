// SPDX-License-Identifier: GPL-3.0-or-later
#include "interrupt.hpp"

#include <signal.h>

namespace etude::helper {

namespace {

volatile sig_atomic_t g_raised = 0;
volatile sig_atomic_t g_unacted = 0; // cut a wait short, not yet acted on at a checkpoint
volatile sig_atomic_t g_seen = 0;

void on_signal(int) { g_raised = 1; }

} // namespace

void install_interrupt_handlers() {
    struct sigaction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
}

bool interrupt_in_wait() {
    if (g_raised == 0) {
        return false;
    }
    g_raised = 0;
    g_unacted = 1;
    g_seen = 1;
    return true;
}

bool interrupt_pending() { return g_raised != 0 || g_unacted != 0; }

bool consume_interrupt() {
    if (g_raised == 0 && g_unacted == 0) {
        return false;
    }
    g_raised = 0;
    g_unacted = 0;
    g_seen = 1;
    return true;
}

bool was_interrupted() { return g_seen != 0; }

void reset_interrupt() {
    g_raised = 0;
    g_unacted = 0;
    g_seen = 0;
}

} // namespace etude::helper
