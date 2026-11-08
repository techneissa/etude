// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_label_fade.hpp"

#include <algorithm>

namespace etude {
namespace {

float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

void ChordLabelFade::reset() {
    arriving_ = -1;
    arriving_alpha_ = 0.0f;
    leaving_ = {};
    started_ = false;
    count_ = 0;
}

void ChordLabelFade::update(int index, bool visible, std::int64_t now_ns, bool reduced_effects) {
    // The time since the last frame, never more than one fade: a hitch ends a fade, no further.
    const std::int64_t dt =
        started_ ? std::clamp<std::int64_t>(now_ns - last_ns_, 0, kChordFadeNs) : 0;
    last_ns_ = now_ns;
    started_ = true;
    const float step = static_cast<float>(dt) / static_cast<float>(kChordFadeNs);

    if (reduced_effects) {
        for (ChordLabelAlpha& l : leaving_) {
            l = {-1, 0.0f};
        }
        arriving_ = index;
        arriving_alpha_ = index >= 0 && visible ? 1.0f : 0.0f;
    } else {
        if (index != arriving_) {
            // What was arriving or shown leaves from the opacity it has now; the names already
            // leaving finish their fades, and with no room left the faintest of them goes.
            if (arriving_ >= 0 && arriving_alpha_ > 0.0f) {
                ChordLabelAlpha* slot = &leaving_[0];
                for (ChordLabelAlpha& l : leaving_) {
                    if (l.index < 0) {
                        slot = &l;
                        break;
                    }
                    slot = l.alpha < slot->alpha ? &l : slot;
                }
                *slot = {arriving_, arriving_alpha_};
            }
            arriving_ = index;
            arriving_alpha_ = 0.0f;
            // A label that comes back while it is still fading out picks up where it was.
            for (ChordLabelAlpha& l : leaving_) {
                if (index >= 0 && l.index == index) {
                    arriving_alpha_ = l.alpha;
                    l = {-1, 0.0f};
                }
            }
        }
        const float target = arriving_ >= 0 && visible ? 1.0f : 0.0f;
        arriving_alpha_ = target > arriving_alpha_ ? std::min(arriving_alpha_ + step, target)
                                                   : std::max(arriving_alpha_ - step, target);
        for (ChordLabelAlpha& l : leaving_) {
            if (l.index >= 0) {
                l.alpha = std::max(l.alpha - step, 0.0f);
                l.index = l.alpha > 0.0f ? l.index : -1;
            }
        }
    }

    count_ = 0;
    for (const ChordLabelAlpha& l : leaving_) {
        if (l.index >= 0 && l.alpha > 0.0f) {
            items_[count_++] = {l.index, smooth(l.alpha)};
        }
    }
    if (arriving_ >= 0 && arriving_alpha_ > 0.0f) {
        items_[count_++] = {arriving_, smooth(arriving_alpha_)};
    }
}

} // namespace etude
