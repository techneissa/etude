// SPDX-License-Identifier: GPL-3.0-or-later
#include "key_ring_demo.hpp"

#include "key_ring_layout.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace {

struct TypeChip {
    const char* id;
    const char* words;
    ScaleType type;
};
constexpr std::array<TypeChip, 4> kTypes{{{"##ring-demo-major", "Major", ScaleType::Major},
                                          {"##ring-demo-minor", "Natural minor", ScaleType::NaturalMinor},
                                          {"##ring-demo-dorian", "Dorian", ScaleType::Dorian},
                                          {"##ring-demo-blues", "Blues", ScaleType::Blues}}};

} // namespace

void KeyRingDemo::draw(Kit& k, const Frame& f) {
    if (!open_) {
        return;
    }
    const float gap = k.s(tok::kHitGap);
    const float hit = k.s(tok::kHit);
    const ImVec2 from{0.0f, f.row_max.y};
    if (begin_layer("##key-ring-demo", from, {f.display.x, f.display.y - from.y})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(from, f.display, u32(col::kScreenBg));
        // The chips' row at the bottom, the ring above it; a screen too short for the ring over a
        // row (the ring's keys keep a hit each) puts the chips in a column at the left instead.
        const float row_room = f.display.y - from.y - hit - 3.0f * gap;
        const bool column = row_room < key_ring_min_side(tok::kHit, k.u);
        float widest = 0.0f;
        for (const TypeChip& t : kTypes) {
            widest = std::max(widest, chip_width(k, nullptr, t.words));
        }
        const float chips_y = f.display.y - hit - gap;
        const Box room = column ? Box{from.x + gap + widest + gap, from.y + gap, f.display.x - gap,
                                      f.display.y - gap}
                                : Box{from.x + gap, from.y + gap, f.display.x - gap, chips_y - gap};
        const int tapped = key_ring(k, "##ring-demo", room, root_, type_);
        if (tapped >= 0) {
            root_ = tapped;
        }
        float widths = 0.0f;
        for (const TypeChip& t : kTypes) {
            widths += chip_width(k, nullptr, t.words) + gap;
        }
        float x = column ? from.x + gap : (f.display.x - widths + gap) * 0.5f;
        float y = column ? from.y + gap : chips_y;
        for (const TypeChip& t : kTypes) {
            ChipOpts o;
            o.on = type_ == t.type;
            if (chip(k, t.id, t.words, {x, y}, o)) {
                type_ = t.type;
            }
            if (column) {
                y += hit + gap;
            } else {
                x += chip_width(k, nullptr, t.words) + gap;
            }
        }
    }
    ImGui::End();
}

} // namespace etude::app::ui
