// SPDX-License-Identifier: GPL-3.0-or-later
// SIGTERM and SIGINT, which systemd sends when the update service is stopped (or its timeout ends),
// are caught and only set a flag. The helper reads it at the places where it can stop safely (between
// the steps of an install, while it waits for a program or the new version), and takes the same way
// out as for any other failure: the old app started again, or the old version put back. Part of the
// root update helper (helper/main.cpp).
#pragma once

namespace etude::helper {

// Catches SIGTERM and SIGINT (without SA_RESTART, so a blocking call is cut short).
void install_interrupt_handlers();

// What the handler does: the next `consume_interrupt` returns true.
void raise_interrupt();

// For the checkpoints between steps: true when a signal came since the last checkpoint, including one
// that already cut a wait short (`interrupt_in_wait`) and has not been acted on yet. Clears it, so the
// clean-up that follows is not itself cut short.
bool consume_interrupt();

// True while a signal has come and no checkpoint has acted on it (it is not cleared): for a blocking
// call that was cut short by it (EINTR) and must decide whether to try again.
bool interrupt_pending();

// For a wait (a program, the backup child): true when a signal came, and the wait should end. The
// signal is remembered as not yet acted on, for the next `consume_interrupt`.
bool interrupt_in_wait();

// True when a signal was ever consumed: the reason given for the failure says so.
bool was_interrupted();

// For tests: forget a signal that was raised or consumed.
void reset_interrupt();

} // namespace etude::helper
