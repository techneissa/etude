// SPDX-License-Identifier: GPL-3.0-or-later
// A toast's Undo is offered only while the edit it takes back is the latest.
#include "etude/toast.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("an Undo is refused once anything was edited or undone after the edit, and its toast goes") {
    ToastState t;
    t.show("Sections joined", 0.0, false);
    REQUIRE(t.offer_undo(7, 8));
    CHECK_FALSE(t.resolve_action(8)); // still the latest edit: it stays
    CHECK(t.active());
    CHECK(t.action() == ToastAction::Undo);
    CHECK(t.resolve_action(9)); // a later edit: Undo would take back that one
    CHECK_FALSE(t.active());
    CHECK(t.action() == ToastAction::None);
    // The same for an undo done elsewhere, and a song loaded (the counter moves on both).
    t.show("Cut at Bar 4", 2.0, false);
    REQUIRE(t.offer_undo(9, 10));
    CHECK(t.resolve_action(11));
    CHECK_FALSE(t.active());
}
