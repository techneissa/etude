// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/pointer_keys.hpp"

#include "etude/pitch.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

int velocity_at(float t) {
    t = std::clamp(std::isfinite(t) ? t : 0.0f, 0.0f, 1.0f);
    const float v = static_cast<float>(kPointerMinVelocity) +
                    static_cast<float>(kPointerMaxVelocity - kPointerMinVelocity) * t;
    return std::clamp(static_cast<int>(std::lround(v)), kPointerMinVelocity, kPointerMaxVelocity);
}

// The gap between the point and an axis-aligned box (0 inside it).
float box_distance(float x0, float y0, float x1, float y1, float x, float y) {
    const float dx = std::max({x0 - x, 0.0f, x - x1});
    const float dy = std::max({y0 - y, 0.0f, y - y1});
    return std::hypot(dx, dy);
}

// How far the point is from the key `note` as hit_test_key sees it, in window pixels: 0 on the key.
// A black key is one box; a white key is its lower part (full width) plus its upper part, which is
// the same width less the black keys beside it.
float distance_to_key(const PointerGeometry& g, int note, float x, float y) {
    const RectF& area = g.key_area;
    const int whites = g.layout.white_key_count();
    const float split = area.y + area.h * kBlackKeyLengthShare;
    const auto span = [&](int n, float& x0, float& x1) {
        const KeyRect r = g.view.to_window(g.layout.key(n), whites);
        x0 = area.x + r.x * area.w;
        x1 = area.x + (r.x + r.width) * area.w;
    };
    float x0 = 0.0f;
    float x1 = 0.0f;
    span(note, x0, x1);
    if (is_black_key(note)) {
        return box_distance(x0, area.y, x1, split, x, y);
    }
    float best = box_distance(x0, split, x1, area.bottom(), x, y);
    // The upper part: [x0, x1] minus the neighbouring black keys, left to right.
    float from = x0;
    for (const int n : {note - 1, note + 1}) {
        const KeyRange& range = g.layout.range();
        if (n < range.lowest || n > range.highest || !is_black_key(n)) {
            continue;
        }
        float b0 = 0.0f;
        float b1 = 0.0f;
        span(n, b0, b1);
        if (b1 <= x0 || b0 >= x1) {
            continue;
        }
        if (b0 > from) { // a free stretch ends where this black key begins
            best = std::min(best, box_distance(from, area.y, b0, split, x, y));
        }
        from = std::max(from, b1);
    }
    if (from < x1) {
        best = std::min(best, box_distance(from, area.y, x1, split, x, y));
    }
    return best;
}

} // namespace

std::optional<KeyHit> hit_test_key(const PointerGeometry& g, float x, float y) {
    const RectF& area = g.key_area;
    if (area.empty() || !std::isfinite(x) || !std::isfinite(y) || x < area.x || y < area.y ||
        x >= area.right() || y >= area.bottom()) {
        return std::nullopt;
    }
    const float u = (x - area.x) / area.w; // 0..1 across the window
    const int whites = g.layout.white_key_count();
    const float black_length = area.h * kBlackKeyLengthShare;
    const KeyRange& range = g.layout.range();
    const auto covers = [&](int note) {
        const KeyRect r = g.view.to_window(g.layout.key(note), whites);
        return r.width > 0.0f && u >= r.x && u < r.x + r.width;
    };
    if (y < area.y + black_length) {
        for (int note = range.lowest; note <= range.highest; ++note) {
            if (is_black_key(note) && covers(note)) {
                return KeyHit{note,
                              static_cast<std::uint8_t>(velocity_at((y - area.y) / black_length))};
            }
        }
    }
    for (int note = range.lowest; note <= range.highest; ++note) {
        if (!is_black_key(note) && covers(note)) {
            return KeyHit{note, static_cast<std::uint8_t>(velocity_at((y - area.y) / area.h))};
        }
    }
    return std::nullopt;
}

FingerAction finger_action(FingerPhase phase, bool direct_touch, bool ui_owns_touch,
                           bool have_geometry) {
    if (!direct_touch) {
        return FingerAction::Ignore;
    }
    switch (phase) {
    case FingerPhase::Up:
    case FingerPhase::Cancelled:
        return FingerAction::Release;
    case FingerPhase::Down:
        return have_geometry && !ui_owns_touch ? FingerAction::Press : FingerAction::Ignore;
    case FingerPhase::Motion:
        return have_geometry ? FingerAction::Move : FingerAction::Ignore;
    }
    return FingerAction::Ignore;
}

PointerKeys::Pointer* PointerKeys::find(PointerId id) {
    for (Pointer& p : pointers_) {
        if (p.used && p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const PointerKeys::Pointer* PointerKeys::find(PointerId id) const {
    return const_cast<PointerKeys*>(this)->find(id);
}

void PointerKeys::press(int note, int velocity, std::int64_t time_ns, std::vector<MidiEvent>& out) {
    if (shared_[static_cast<std::size_t>(note)]++ != 0) {
        return;
    }
    MidiEvent e;
    e.time_ns = time_ns;
    e.type = MidiEventType::NoteOn;
    e.data1 = static_cast<std::uint8_t>(note);
    e.data2 = static_cast<std::uint8_t>(velocity);
    e.source = InputSource::Pointer;
    out.push_back(e);
}

void PointerKeys::release(int note, std::int64_t time_ns, std::vector<MidiEvent>& out) {
    if (note < 0 || --shared_[static_cast<std::size_t>(note)] != 0) {
        return;
    }
    MidiEvent e;
    e.time_ns = time_ns;
    e.type = MidiEventType::NoteOff;
    e.data1 = static_cast<std::uint8_t>(note);
    e.source = InputSource::Pointer;
    out.push_back(e);
}

void PointerKeys::down(PointerId id, float x, float y, const PointerGeometry& g,
                       std::int64_t time_ns, std::vector<MidiEvent>& out) {
    if (find(id) != nullptr) {
        move(id, x, y, g, time_ns, out); // a second down for a pointer already down
        return;
    }
    const std::optional<KeyHit> hit = hit_test_key(g, x, y);
    if (!hit) {
        return;
    }
    for (Pointer& p : pointers_) {
        if (!p.used) {
            p = {true, id, hit->note};
            press(hit->note, hit->velocity, time_ns, out);
            return;
        }
    }
    // More pointers than slots: the extra one plays nothing.
}

void PointerKeys::move(PointerId id, float x, float y, const PointerGeometry& g,
                       std::int64_t time_ns, std::vector<MidiEvent>& out) {
    Pointer* p = find(id);
    if (p == nullptr) {
        return;
    }
    const std::optional<KeyHit> hit = hit_test_key(g, x, y);
    const int now = hit ? hit->note : -1;
    if (now == p->note) {
        return;
    }
    if (p->note >= 0 && g.slide_margin_px > 0.0f &&
        distance_to_key(g, p->note, x, y) <= g.slide_margin_px) {
        return; // still within the margin of the key it is on
    }
    const int old = p->note;
    p->note = now;
    release(old, time_ns, out);
    if (hit) {
        press(hit->note, hit->velocity, time_ns, out);
    }
}

void PointerKeys::up(PointerId id, std::int64_t time_ns, std::vector<MidiEvent>& out) {
    Pointer* p = find(id);
    if (p == nullptr) {
        return;
    }
    const int note = p->note;
    *p = {};
    release(note, time_ns, out);
}

void PointerKeys::release_all(std::int64_t time_ns, std::vector<MidiEvent>& out) {
    for (Pointer& p : pointers_) {
        if (p.used) {
            const int note = p.note;
            p = {};
            release(note, time_ns, out);
        }
    }
}

int PointerKeys::active_pointers() const {
    int n = 0;
    for (const Pointer& p : pointers_) {
        n += p.used ? 1 : 0;
    }
    return n;
}

int PointerKeys::held_notes() const {
    int n = 0;
    for (const std::uint8_t c : shared_) {
        n += c > 0 ? 1 : 0;
    }
    return n;
}

} // namespace etude
