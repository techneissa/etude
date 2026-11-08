// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace etude {

// How much of the look the renderer can afford. Each level drops
// one more effect, in this fixed order: the shaders first (a renderer that has none never visits
// that level, see GovernorConfig::shader_level), then particles, then the motion background, then
// half the glow width, then the style extras (frost grain, specular streaks, gloss bands, rims).
// Notes and keys are never dropped: no level has a switch for them.
enum class EffectsLevel : std::uint8_t {
    Full,
    NoShaders,
    NoParticles,
    NoMotion,
    NarrowGlow,
    NoStyleExtras
};
inline constexpr std::array<EffectsLevel, 6> kAllEffectsLevels{
    EffectsLevel::Full,       EffectsLevel::NoShaders,  EffectsLevel::NoParticles,
    EffectsLevel::NoMotion,   EffectsLevel::NarrowGlow, EffectsLevel::NoStyleExtras};
inline constexpr EffectsLevel kLowestEffectsLevel = EffectsLevel::NoStyleExtras;

// What a level leaves switched on. The look's own switches still apply on top (a level never turns
// an effect on that the look has off).
struct EffectsBudget {
    bool shaders = true; // the shader versions of an effect (the Neon glow on the gpu and gl renderers)
    bool particles = true;
    bool motion_background = true;
    float glow_width = 1.0f; // multiplier on every glow's width
    bool style_extras = true;
};

EffectsBudget effects_budget(EffectsLevel level);
// "full", "no shaders", "no particles", "no motion", "narrow glow", "no style extras".
std::string_view effects_level_name(EffectsLevel level);

struct GovernorConfig {
    std::int64_t budget_ns = 16'666'667; // one frame at 60 Hz
    // The renderer draws something with a shader, so NoShaders is a real step down: the first. Off,
    // the governor goes from Full straight to NoParticles and back, as it did before any shader.
    bool shader_level = false;
    // The moving average of frame times counts as over budget above `budget_ns * over_ratio`, and
    // as headroom below `budget_ns * headroom_ratio`. The gap between them is the hysteresis.
    double over_ratio = 1.0;
    double headroom_ratio = 0.7;
    // Weight of the newest frame in the moving average for one frame of `smoothing_period_ns`
    // (60 Hz). A frame of another length weighs 1 - (1 - smoothing)^(length / period), so the
    // average smooths over the same time at every refresh rate.
    double smoothing = 0.1;
    std::int64_t smoothing_period_ns = 16'666'667;
    // The windows are in time, so 120 Hz behaves like 60 Hz.
    std::int64_t down_ns = 500'000'000;  // over budget this long before stepping down
    std::int64_t up_ns = 5'000'000'000;  // headroom this long before stepping up
    // A step up that runs over budget again within `up_ns` doubles the headroom needed for the
    // next step up, up to this factor, so the level does not flap.
    int max_up_backoff = 8;
};

// What one frame cost, for EffectsGovernor::add_frame. With vsync the time between frames is the
// refresh period however light the frame was, so the cost is the frame's own work (`work_ns`,
// from the start of drawing to after the present), unless the frame missed a refresh: then the
// time since the last frame (`interval_ns`, more than 1.5 periods) is what it really cost.
std::int64_t frame_cost_ns(std::int64_t work_ns, std::int64_t interval_ns, std::int64_t period_ns);

// Picks the effects level from measured frame times. Pure logic: the caller measures each frame
// and applies the level it returns.
class EffectsGovernor {
  public:
    explicit EffectsGovernor(GovernorConfig config = {});

    // Adds one frame's cost and returns the level to draw the next frame at. `interval_ns` is the
    // time since the previous frame (the budget when 0): it advances the windows and weighs the
    // average. A cost that is not positive is ignored.
    EffectsLevel add_frame(std::int64_t frame_ns, std::int64_t interval_ns = 0);
    // The loop stopped drawing and starts again: the gap is deliberate, so the runs of over-budget
    // and headroom time start over (the level and the average stay), and the frame counts for its
    // own cost, as one frame of the budget.
    EffectsLevel add_frame_after_gap(std::int64_t frame_ns);

    // Whether the frame has a shader effect to drop (the renderer has the shader and the style
    // uses it), so NoShaders is a real step. Cheap: the app sets it every frame, so a change of
    // style takes effect at once. Turning it off while at NoShaders returns to Full, since nothing
    // was left to drop.
    void set_shader_level(bool on);

    EffectsLevel level() const { return level_; }
    // The most degraded level reached since construction or reset().
    EffectsLevel lowest() const { return lowest_; }
    double average_ns() const { return average_ns_; }
    int up_backoff() const { return backoff_; }
    const GovernorConfig& config() const { return config_; }

    void reset();

  private:
    // The level one step down (`dir` 1) or up (-1), skipping NoShaders when there are no shaders.
    EffectsLevel stepped(EffectsLevel level, int dir) const;

    GovernorConfig config_;
    EffectsLevel level_ = EffectsLevel::Full;
    EffectsLevel lowest_ = EffectsLevel::Full;
    double average_ns_ = 0.0;
    bool has_average_ = false;
    std::int64_t over_ns_ = 0;
    std::int64_t headroom_ns_ = 0;
    int backoff_ = 1;
    std::int64_t since_up_ns_ = INT64_MAX / 2;
};

} // namespace etude
