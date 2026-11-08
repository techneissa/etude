// SPDX-License-Identifier: GPL-3.0-or-later
// Developer: the combo as a function of time, for `etude --bench --combo` (the frame cost of the
// number) and `--combo-demo NAME` (the frames of the looping GIFs). A timeline is a
// list of (second, combo) steps; the combo at a time is the last step at or before it. `lead_s`
// is stepped but not recorded, so a recording starts with the display settled.
#pragma once

#include <string_view>
#include <vector>

namespace etude::app {

class ComboTimeline {
  public:
    // "appear" (8 up to 14: the number shows at 10, then each change), "pulse" (22 up to 28: the
    // milestone at 25), "break" (28 to 30, lost, and a new run) or "perf" (endless: climbs to 130,
    // breaks, over and over). An unknown name is not valid().
    explicit ComboTimeline(std::string_view name) {
        if (name == "appear") {
            lead_s_ = 0.8;
            record_s_ = 6.0;
            steps_ = {{0.0, 8},   {1.35, 9},  {1.9, 10}, {2.45, 11}, {3.0, 12},
                      {3.55, 13}, {4.1, 14},  {5.6, 0}};
        } else if (name == "pulse") {
            lead_s_ = 1.0;
            record_s_ = 4.6;
            steps_ = {{0.0, 22}, {1.5, 23}, {2.1, 24}, {2.7, 25}, {3.3, 26}, {3.9, 27}, {5.1, 0}};
        } else if (name == "break") {
            lead_s_ = 0.8;
            record_s_ = 5.0;
            steps_ = {{0.0, 28}, {1.4, 29}, {2.0, 30}, {3.2, 0}, {3.9, 1}, {4.5, 2}};
        } else if (name == "perf") {
            period_s_ = 27.0;
            for (int c = 0; c <= 130; ++c) {
                steps_.push_back({0.2 * c, c});
            }
            steps_.push_back({26.4, 0});
        }
    }

    bool valid() const { return !steps_.empty(); }
    double lead_s() const { return lead_s_; }
    double record_s() const { return record_s_; }

    int combo_at(double t) const {
        if (period_s_ > 0.0) {
            t -= period_s_ * static_cast<double>(static_cast<long long>(t / period_s_));
        }
        int combo = 0;
        for (const Step& s : steps_) {
            if (s.t > t) {
                break;
            }
            combo = s.combo;
        }
        return combo;
    }

  private:
    struct Step {
        double t;
        int combo;
    };
    std::vector<Step> steps_;
    double lead_s_ = 0.0;
    double record_s_ = 0.0;
    double period_s_ = 0.0; // > 0: the timeline repeats
};

} // namespace etude::app
