// SPDX-License-Identifier: GPL-3.0-or-later
// The rows of a song's details card that read the song's stored facts: its length, level and hands
// with the practice choices under them, and its best score with the runs (moved out of
// library_screen_details.cpp as they were).
#include "library_screen_shared.hpp"

namespace etude::app::ui::lib_shared {

namespace {

// The player's hands and speed for the song, one row: "Practice", the hands, a gauge, the speed.
void draw_practice_row(const Kit& k, ImDrawList* dl, float tx, float cy, float size,
                       const SongPractice& practice) {
    const float isz = k.s(tok::kIconSm);
    const float pmid = cy + size * 0.6f;
    float px = tx;
    const char* head = "Practice";
    const float head_w = text_size(k.regular, size, head).x;
    label_at(k, dl, k.regular, size, {px, pmid - isz, px + head_w, pmid + isz}, LabelAlign::Left,
             col::kText2, head);
    px += head_w + k.s(tok::kS2);
    draw_hands(k, dl, px, pmid, isz, practice.hands != HandFilter::Right,
               practice.hands != HandFilter::Left);
    px += hands_width(isz) + k.s(tok::kS4);
    glyph(k, dl, icon::kGauge, {px + isz * 0.5f, pmid}, isz, col::kText2);
    px += isz + k.s(tok::kS1);
    const UnitText speed = format_unit(practice.speed_percent, Unit::Percent, 0);
    const float speed_w = text_size(k.regular, size, speed.c_str()).x;
    label_at(k, dl, k.regular, size, {px, pmid - isz, px + speed_w, pmid + isz}, LabelAlign::Left,
             col::kText, speed.c_str());
}

} // namespace

// The facts (length, stars with the level word, hands) and the practice row under them; `cy` moves
// down past both.
void draw_details_facts(Kit& k, ImDrawList* dl, const CatalogueSong& s, const SongPractice& practice,
                        float tx, float& cy) {
    const float size = k.s(tok::kLabel);
    const float mid = cy + size * 0.6f;
    float fx = tx;
    FitText len;
    length_text(len, s.duration_s);
    glyph(k, dl, icon::kTimer, {fx + k.s(tok::kIconSm) * 0.5f, mid}, k.s(tok::kIconSm),
          col::kText2);
    fx += k.s(tok::kIconSm) + k.s(tok::kS1);
    const float len_w = text_size(k.regular, size, len.c_str()).x;
    const float row = k.s(tok::kIconSm);
    label_at(k, dl, k.regular, size, {fx, mid - row, fx + len_w, mid + row}, LabelAlign::Left,
             col::kText, len.c_str());
    fx += len_w + k.s(tok::kS4);
    draw_level_stars(k, dl, fx, mid, s.half_stars);
    fx += level_stars_width(k) + k.s(tok::kS2);
    const char* word = level_word(s.difficulty);
    const float word_w = text_size(k.regular, size, word).x;
    label_at(k, dl, k.regular, size, {fx, mid - row, fx + word_w, mid + row}, LabelAlign::Left,
             col::kText, word);
    fx += word_w + k.s(tok::kS4);
    draw_hands(k, dl, fx, mid, k.s(tok::kIconSm), s.left, s.right);
    cy += size * 1.4f + k.s(tok::kS4);
    draw_practice_row(k, dl, tx, cy, size, practice);
    cy += size * 1.4f + k.s(tok::kS3);
}

// The best score as a ring with its percent, and beside it the runs and when the song was last played;
// `cy` moves down past them.
void draw_details_score(Kit& k, ImDrawList* dl, const LibraryPort& port, const CatalogueSong& s, float tx,
                        float right, float& cy) {
    const float r = k.s(tok::kFace) * 0.5f;
    const ImVec2 rc{tx + r + k.s(lib_tok::kRing), cy + r};
    draw_ring(k, dl, rc, r, s.best_permille);
    FitText pct;
    if (s.best_permille >= 0) {
        pct.assign(
            format_unit(static_cast<double>(s.best_permille) / 10.0, Unit::Percent, 0).view());
    } else {
        pct.assign("\xE2\x80\x93");
    }
    const float ps = k.s(tok::kCaption);
    label_at(k, dl, k.bold, ps, {rc.x - r, rc.y - r, rc.x + r, rc.y + r}, LabelAlign::Centre,
             col::kText, pct.c_str());
    const float lx = rc.x + r + k.s(tok::kS4);
    FitText runs;
    FitText when;
    if (port.history_loading()) {
        runs.assign("Reading the practice history\xE2\x80\xA6");
    } else if (s.runs == 0) {
        runs.assign("Not played yet");
    } else {
        runs.format("%d %s", s.runs, s.runs == 1 ? "run" : "runs");
        const int days = port.today() - s.last_day;
        if (days <= 0) {
            when.assign("Played today");
        } else if (days == 1) {
            when.assign("Played yesterday");
        } else {
            when.format("Played %d days ago", days);
        }
    }
    const float bs = k.s(tok::kLabel);
    const FitText runs_cut = fit(k.bold, bs, runs.view(), right - lx);
    const StackLine runs_line{k.bold, bs, col::kText, runs_cut.c_str()};
    const StackLine when_line{k.regular, k.s(tok::kCaption), col::kText2, when.c_str()};
    label_stack(k, dl, {lx, rc.y - r, right, rc.y + r}, runs_line,
                when.empty() ? nullptr : &when_line);
    cy += 2.0f * r + k.s(tok::kS4);
}

} // namespace etude::app::ui::lib_shared
