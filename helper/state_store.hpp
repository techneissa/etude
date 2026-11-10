// SPDX-License-Identifier: GPL-3.0-or-later
// The helper's own folder, <state dir> (root's alone, mode 0700): the state of an update in
// progress, written whole and flushed, and the two locks that keep two runs apart. Part of the root
// update helper (helper/main.cpp).
#pragma once

#include "context.hpp"
#include "fd.hpp"

#include "etude/update_state.hpp"

#include <string>

namespace etude::helper {

// Opens the state folder, making it (root, 0700) when it is not there. Refuses one that is a link,
// is not root's or can be entered by others. `error` says why not.
Fd open_state_dir(const Context& c, std::string& error);

// An exclusive lock on a file in the state folder, held until the object goes. `wait` false gives up
// at once when another holds it; `wait_s` above 0 waits that long at most, and then gives up the same way.
class StateLock {
  public:
    StateLock(const Context& c, const char* name, bool wait, std::string& error, double wait_s = 0);
    bool held() const { return fd_.ok(); }
    // Not held because another run does hold it (as against trouble with the folder or the file).
    bool contended() const { return contended_; }

  private:
    Fd fd_;
    bool contended_ = false;
};

// The helper acting on the app (a stop, a kill, a restart, a start): the `acting.lock` is held for as long
// as it does, and `acting.stamp` says when it last began or ended, so `idle` (the crash screen's
// ExecCondition) can tell a failure the helper's own stop caused from a crash. Takes the lock like
// StateLock (`wait_s` seconds at most) and acts even when it could not.
inline constexpr int kActingWindowS = 130; // longer than a unit's stop time out (90 s) and the kills after it

class ActingScope {
  public:
    ActingScope(const Context& c, int wait_s);
    ~ActingScope();
    ActingScope(const ActingScope&) = delete;
    ActingScope& operator=(const ActingScope&) = delete;
    bool held() const { return lock_.held(); }

  private:
    const Context& c_;
    std::string error_;
    StateLock lock_;
};

// The helper is done acting and starts the crash screen itself: nothing it did before is a reason to hold
// the screen back (`idle` then ends 0 whatever the unit's last failure looked like).
void forget_acting(const Context& c);

// Whole seconds since the helper last began or ended acting on the app; -1 when it never did, or the
// stamp cannot be read.
long seconds_since_acting(const Context& c);

// The state of an update in progress; phase None when there is none. A file that is there but cannot
// be read, is not root's or does not parse is set aside (as `state.unreadable`), reported in `error`
// and flagged in `*damaged`: the caller must not take it for "no update" (see settle.cpp, which
// works out what was going on from the links).
PendingUpdate load_pending(const Context& c, std::string& error, bool* damaged = nullptr);

// The state as it stands, with no lock and no side effect (a damaged file is not set aside): for a
// caller that cannot take the lock or trust the layout and only needs to know whether a going back is
// recorded. None when there is nothing, or nothing that can be read (`*readable`, when given, says a
// record is there but cannot be: the caller that needs to tell "no record" from that asks).
PendingUpdate peek_pending(const Context& c, bool* readable = nullptr);

bool save_pending(const Context& c, const PendingUpdate& state, std::string& error);

#if defined(ETUDE_HELPER_TEST_HOOKS)
// Tests only: the `nth` next `save_pending` (1 is the next) fails, once. 0 clears it.
void fail_save_in_test(int nth);
// Tests only: how many times a blocked lock wait was cut short by a signal and went on waiting. A
// test that sends signals reads it to know one reached the waiter inside `flock`.
int lock_signals_seen_in_test();
#endif

} // namespace etude::helper
