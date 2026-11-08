// SPDX-License-Identifier: GPL-3.0-or-later
// "Add to Melodies" on a song's details card: the step picker, and what it stores. The songs the
// player adds are kept per profile by content hash in `AppSettings::melody_added` (core
// melody_added.hpp); the Melodies page's "From your library" row shows them first, at the step chosen.
#include "library_screen.hpp"

#include "library_screen_shared.hpp"

#include "../library_row.hpp"

#include "icons.hpp"

#include "etude/melody_steps.hpp"

#include <algorithm>
#include <cstdio>

namespace etude::app::ui {

using namespace lib_shared;

const MelodyAdded& LibraryScreen::melody_list(const AppSettings& app) {
    if (app.melody_added != melody_added_text_) {
        melody_added_text_ = app.melody_added;
        melody_added_ = MelodyAdded::parse(app.melody_added);
    }
    return melody_added_;
}

void LibraryScreen::open_melody_pick(const AppSettings& app, const CatalogueSong& song) {
    melody_suggested_ = suggest_melody_step(step_facts(song));
    const int own = melody_list(app).step_of(song.hash, song.relative);
    melody_pick_ = own != 0 ? own : melody_suggested_;
    many_ = false;
    over_ = Over::MelodyStep;
}

namespace {

// What the picker's two lines say: how the step came to be offered, and what the chosen one asks.
void picker_words(FitText& offer, FitText& chosen, int suggested, int own, int pick) {
    if (own != 0) {
        offer.format("In Melodies at step %d. Tap another to move it.", own);
    } else if (suggested != 0) {
        offer.format("Step %d is suggested. Tap another to choose.", suggested);
    } else {
        offer.assign("No step is suggested for this song. Tap one to choose.");
    }
    if (pick != 0) {
        chosen.format("%d \xC2\xB7 %s", pick, melody_step_words(pick));
    } else {
        chosen.assign("1 asks the least, 5 the most");
    }
}

} // namespace

void LibraryScreen::draw_melody_step(Kit& k, UiModel& model, LibraryPort& port, const Box& area) {
    const LibraryCatalogue& cat = port.catalogue();
    if (over_song_ >= cat.size() || cat.song(over_song_).hash == 0) {
        over_ = Over::None;
        return;
    }
    const std::uint64_t hash = cat.song(over_song_).hash;
    const int own = melody_list(model.app).step_of(hash, cat.song(over_song_).relative);
    const float margin = k.s(lib_tok::kSidePad);
    const float pad = k.s(tok::kS4);
    const float hit = k.s(tok::kHit);
    // A step is a chip, at least `kChipMinW` across and so more than a hit.
    const float cell = std::max(hit, k.s(tok::kChipMinW));
    const float steps_w = static_cast<float>(kMelodyLastStep) * cell +
                          static_cast<float>(kMelodyLastStep - 1) * k.s(tok::kHitGap);
    const float w = std::min(std::max(k.s(lib_tok::kConfirmW), steps_w + 2.0f * pad),
                             area.w() - 2.0f * margin);
    const float ts = k.s(tok::kTitle);
    const float ls = k.s(tok::kLabel);
    const float rows = own != 0 ? 2.0f : 1.0f; // Cancel and the add, then Remove when it is added
    const float h = pad * 2.0f + ts * 1.3f + ls * 1.5f + k.s(tok::kS3) + hit + k.s(tok::kS2) +
                    ls * 1.5f + k.s(tok::kS4) + rows * hit + (rows - 1.0f) * k.s(tok::kHitGap);
    // On whole pixels, so the icons on the card sit centred in their buttons.
    const float x = std::floor(area.cx() - w * 0.5f);
    const float y = std::floor(area.y0 + std::max(margin, (area.h() - h) * 0.5f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    sheet_card(k, dl, {x, y}, {x + w, y + h}, k.s(tok::kRadiusSheet), col::kSurface);
    k.toast_avoid({x, y, w, h});
    float cy = y + pad;
    label_at(k, dl, k.bold, ts, {x + pad, cy, x + w - pad, cy + ts * 1.3f}, LabelAlign::Left, col::kText,
             "Add to Melodies");
    cy += ts * 1.3f;
    FitText offer;
    FitText chosen;
    picker_words(offer, chosen, melody_suggested_, own, melody_pick_);
    const FitText offer_cut = fit(k.regular, ls, offer.view(), w - 2.0f * pad);
    label_at(k, dl, k.regular, ls, {x + pad, cy, x + w - pad, cy + ls * 1.5f}, LabelAlign::Left,
             col::kText2, offer_cut.c_str());
    cy += ls * 1.5f + k.s(tok::kS3);
    // The five steps in a row, centred: a hit across each, the chosen one lit.
    float sx = std::floor(x + (w - steps_w) * 0.5f);
    for (int step = kMelodyFirstStep; step <= kMelodyLastStep; ++step) {
        ImGui::PushID(step);
        char number[4];
        std::snprintf(number, sizeof number, "%d", step);
        ChipOpts o;
        o.on = step == melody_pick_;
        o.width = cell;
        o.fixed = true;
        if (chip(k, "##melody-step", number, {sx, std::round(cy)}, o)) {
            melody_pick_ = step;
        }
        ImGui::PopID();
        sx += cell + k.s(tok::kHitGap);
    }
    cy += hit + k.s(tok::kS2);
    if (!chosen.empty()) {
        const FitText chosen_cut = fit(k.regular, ls, chosen.view(), w - 2.0f * pad);
        label_at(k, dl, k.regular, ls, {x + pad, cy, x + w - pad, cy + ls * 1.5f}, LabelAlign::Centre,
                 col::kText, chosen_cut.c_str());
    }
    cy += ls * 1.5f + k.s(tok::kS4);
    melody_buttons(k, model, port, hash, own, {x, y, x + w, y + h}, std::round(cy));
}

// The picker's buttons from `y` down: Cancel and the add or move, then Remove when the song is in.
void LibraryScreen::melody_buttons(Kit& k, UiModel& model, LibraryPort& port, std::uint64_t hash, int own,
                                   const Box& card, float y) {
    const std::string& path = port.catalogue().song(over_song_).relative;
    const float pad = k.s(tok::kS4);
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const float x = card.x0;
    const float w = card.w();
    const float bw = (w - 2.0f * pad - gap) * 0.5f;
    if (action_button(k, "##melody-cancel", icon::kClose, "Cancel", {x + pad, y}, {bw, hit}, true)) {
        over_ = Over::Details;
        return;
    }
    FitText add;
    if (melody_pick_ == 0) {
        add.assign("Choose a step");
    } else {
        add.format(own == 0 ? "Add to step %d" : (own == melody_pick_ ? "In step %d" : "Move to step %d"),
                   melody_pick_);
    }
    const bool can_add = melody_pick_ != 0 && melody_pick_ != own;
    if (action_button(k, "##melody-add", own == 0 ? icon::kPlus : icon::kMelodies, add.c_str(),
                      {x + pad + bw + gap, y}, {bw, hit}, can_add, true)) {
        MelodyAdded list = melody_list(model.app);
        if (list.set(hash, melody_pick_, path, hidden_added(port.catalogue(), list))) {
            model.app.melody_added = list.text();
            FitText said;
            said.format(own == 0 ? "Added to Melodies, step %d" : "Moved to step %d", melody_pick_);
            k.say(said.view());
            over_ = Over::Details;
        } else {
            k.say("Melodies keeps up to 24 songs. Take one out first.", true);
        }
        return;
    }
    if (own != 0 &&
        action_button(k, "##melody-remove", icon::kClear, "Remove from Melodies",
                      {x + pad, std::round(y + hit + k.s(tok::kHitGap))}, {w - 2.0f * pad, hit}, true,
                      false, true)) {
        MelodyAdded list = melody_list(model.app);
        list.remove(hash, path);
        model.app.melody_added = list.text();
        k.say("Removed from Melodies");
        over_ = Over::Details;
    }
}

} // namespace etude::app::ui
