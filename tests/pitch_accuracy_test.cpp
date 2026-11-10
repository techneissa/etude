// SPDX-License-Identifier: GPL-3.0-or-later
// CI accuracy gate for the pitch detector on the synthetic piano set: every third key A0-C8 at two
// velocities, with room noise and an interfering tone. Targets: under 0.5 % wrong notes, under
// 0.2 % octave errors, decisions within 40 ms from C4 up. Fails if accuracy or that latency drops.
// tools/pitch-eval runs the full set; this gate uses every third key.
#include "etude/pitch_eval.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr int kKeyStep = 3; // every third key

void check_targets(PitchMethod method) {
    SynthEvalOptions options;
    options.velocities = {0.4f, 0.9f};
    options.variants = 1;
    options.note_step = kKeyStep;
    options.seed = 11;
    options.clip.note_s = 0.2f;
    options.detector.method = method;
    const EvalReport r = evaluate_synthetic(options);
    REQUIRE(r.total.notes == 2 * ((kPianoHighNote - kPianoLowNote) / kKeyStep + 1));
    CHECK(rate(r.total.wrong, r.total.notes) < 0.005f);
    CHECK(rate(r.total.octave, r.total.notes) < 0.002f);
    // Not a detector that stays safe by never answering.
    CHECK(rate(r.total.correct, r.total.notes) > 0.95f);
    float c4_up_max_ms = 0.0f;
    for (int reg = register_of(60); reg < kRegisterCount; ++reg) {
        c4_up_max_ms = std::max(c4_up_max_ms,
                                percentile(r.registers[static_cast<size_t>(reg)].latency_ms, 100));
    }
    CHECK(c4_up_max_ms <= 40.0f);
}

} // namespace

TEST_CASE("pitch accuracy: McLeod meets the accuracy targets on the synthetic piano set") {
    check_targets(PitchMethod::Mpm);
}
