// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include "etude/renderer_recovery.hpp"

#include <string>

using namespace etude;

namespace {

constexpr auto kLost = LossCause::DeviceLost;

} // namespace

TEST_CASE("nothing is pending until the device is reported lost") {
    RendererRecovery recovery(RendererPath::Gpu);
    CHECK_FALSE(recovery.pending());
    recovery.note_present(true);
    CHECK_FALSE(recovery.pending());
    recovery.note_loss(kLost, 10);
    CHECK(recovery.pending());
    CHECK(recovery.cause() == LossCause::DeviceLost);
}

TEST_CASE("the first recovery tries the path in use, then basic") {
    RendererRecovery recovery(RendererPath::Gl);
    recovery.note_loss(kLost, 10);
    const RecoveryPlan plan = recovery.begin(10);
    CHECK_FALSE(plan.give_up);
    REQUIRE(plan.count == 2);
    CHECK(plan.tries[0] == RendererPath::Gl);
    CHECK(plan.tries[1] == RendererPath::Basic);
    CHECK_FALSE(recovery.pending());
}

TEST_CASE("basic has nothing to fall back to") {
    RendererRecovery recovery(RendererPath::Basic);
    recovery.note_loss(kLost, 10);
    const RecoveryPlan plan = recovery.begin(10);
    REQUIRE(plan.count == 1);
    CHECK(plan.tries[0] == RendererPath::Basic);
}

TEST_CASE("every kind of report asks for a recovery") {
    for (const LossCause cause : {LossCause::DeviceLost, LossCause::DeviceReset,
                                  LossCause::TargetsReset, LossCause::Simulated}) {
        RendererRecovery recovery(RendererPath::Gpu);
        recovery.note_loss(cause, 5);
        CHECK(recovery.pending());
        CHECK(recovery.cause() == cause);
    }
}

TEST_CASE("a present that fails three times in a row is a lost device") {
    RendererRecovery recovery(RendererPath::Gpu);
    recovery.note_present(false);
    recovery.note_present(false);
    CHECK_FALSE(recovery.pending());
    recovery.note_present(false);
    CHECK(recovery.pending());
    CHECK(recovery.cause() == LossCause::PresentFailed);
}

TEST_CASE("a present that works breaks the streak") {
    RendererRecovery recovery(RendererPath::Gpu);
    for (int i = 0; i < 10; ++i) {
        recovery.note_present(false);
        recovery.note_present(false);
        recovery.note_present(true);
    }
    CHECK_FALSE(recovery.pending());
}

TEST_CASE("a present that may fail does not count (a hidden window)") {
    RendererRecovery recovery(RendererPath::Gpu);
    for (int i = 0; i < 20; ++i) {
        recovery.note_present(false, false);
    }
    CHECK_FALSE(recovery.pending());
}

TEST_CASE("the report that came first names the cause") {
    RendererRecovery recovery(RendererPath::Gpu);
    recovery.note_loss(LossCause::DeviceLost, 4);
    for (int i = 0; i < 3; ++i) {
        recovery.note_present(false);
    }
    CHECK(recovery.cause() == LossCause::DeviceLost);
}

TEST_CASE("the echo of a recovery asks for nothing") {
    RendererRecovery recovery(RendererPath::Gpu);
    recovery.note_loss(kLost, 100);
    recovery.begin(100);
    recovery.note_loss(LossCause::DeviceReset, 101);
    recovery.note_loss(LossCause::TargetsReset, 102);
    CHECK_FALSE(recovery.pending());
    recovery.note_loss(LossCause::DeviceReset, 103);
    CHECK(recovery.pending());
}

TEST_CASE("a simulated loss is never an echo") {
    RendererRecovery recovery(RendererPath::Gpu);
    recovery.note_loss(kLost, 100);
    recovery.begin(100);
    recovery.note_loss(LossCause::Simulated, 101);
    CHECK(recovery.pending());
}

TEST_CASE("a loss soon after a recovery means the path keeps failing: basic only") {
    RendererRecovery recovery(RendererPath::Gpu);
    recovery.note_loss(kLost, 100);
    recovery.finish(recovery.begin(100).tries[0]);
    CHECK(recovery.path() == RendererPath::Gpu);
    recovery.note_loss(kLost, 150);
    const RecoveryPlan again = recovery.begin(150);
    REQUIRE(again.count == 1);
    CHECK(again.tries[0] == RendererPath::Basic);
}

TEST_CASE("a loss long after a recovery starts over with the path in use") {
    RendererRecovery recovery(RendererPath::Gpu);
    recovery.note_loss(kLost, 100);
    recovery.begin(100);
    recovery.note_loss(kLost, 100 + RendererRecovery::kRepeatFrames + 1);
    const RecoveryPlan plan = recovery.begin(100 + RendererRecovery::kRepeatFrames + 1);
    REQUIRE(plan.count == 2);
    CHECK(plan.tries[0] == RendererPath::Gpu);
}

TEST_CASE("the path the recovery ended on is the one used next") {
    RendererRecovery recovery(RendererPath::Gpu);
    recovery.note_loss(kLost, 100);
    recovery.begin(100);
    recovery.finish(RendererPath::Basic); // gpu could not be made again
    CHECK(recovery.path() == RendererPath::Basic);
    recovery.note_loss(kLost, 1000);
    const RecoveryPlan plan = recovery.begin(1000);
    REQUIRE(plan.count == 1);
    CHECK(plan.tries[0] == RendererPath::Basic);
}

TEST_CASE("a loss that keeps coming ends the run instead of looping") {
    RendererRecovery recovery(RendererPath::Gpu);
    long frame = 100;
    int served = 0;
    for (; served < 10; ++served, frame += 10) {
        recovery.note_loss(kLost, frame);
        if (recovery.begin(frame).give_up) {
            break;
        }
    }
    CHECK(served == RendererRecovery::kMaxRepeats + 1);
}

TEST_CASE("giving up does not outlast a quiet spell") {
    RendererRecovery recovery(RendererPath::Basic);
    long frame = 100;
    for (int i = 0; i < 4; ++i, frame += 10) {
        recovery.note_loss(kLost, frame);
        recovery.begin(frame);
    }
    frame += RendererRecovery::kRepeatFrames + 1;
    recovery.note_loss(kLost, frame);
    CHECK_FALSE(recovery.begin(frame).give_up);
}

TEST_CASE("loss causes have words for the log") {
    CHECK(std::string(loss_cause_name(LossCause::DeviceLost)) == "device lost");
    CHECK(std::string(loss_cause_name(LossCause::PresentFailed)) == "present failed");
    CHECK(std::string(loss_cause_name(LossCause::Simulated)) == "simulated device loss");
}
