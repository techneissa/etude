// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_screen.hpp"

#include "library_screen_shared.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "preview.hpp"
#include "../sheet/sheet_preview.hpp"
#include "settings_catalogue.hpp"

#include "etude/level_stars.hpp"
#include "etude/tap_rule.hpp"
#include "etude/unit_format.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront, the IME request

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace etude::app::ui {

using namespace lib_shared;

namespace {

// How many rows of section chips the details sheet shows at most.
constexpr int kSectionRows = 2;

void plan_section_chips(const Kit& k, const SongSections* marked, float width, SectionChips& out) {
    out.chips.clear();
    out.rows = 0;
    if (marked == nullptr || marked->sections.empty()) {
        return;
    }
    const float gap = k.s(tok::kS2);
    const float most = width * 0.6f;
    float x = 0.0f;
    int row = 0;
    for (std::size_t i = 0; i < marked->sections.size(); ++i) {
        const Section& sec = marked->sections[i];
        SectionChips::Chip c;
        c.index = i;
        // The words are cut to a share of the row; the chip adds its own padding.
        std::string words = sec.name;
        if (sec.custom_name && sec.snapped_to_bars && sec.first_measure > 0) {
            words += sec.last_measure > sec.first_measure
                         ? " \xC2\xB7 " + std::to_string(sec.first_measure) + "\xE2\x80\x93" +
                               std::to_string(sec.last_measure)
                         : " \xC2\xB7 " + std::to_string(sec.first_measure);
        }
        c.text = fit(k.regular, k.s(tok::kLabel), words, most - k.s(tok::kS6));
        c.w = chip_width(k, nullptr, c.text.view());
        if (x > 0.0f && x + c.w > width) {
            x = 0.0f;
            ++row;
        }
        if (row >= kSectionRows) {
            break;
        }
        c.x = x;
        c.row = row;
        x += c.w + gap;
        out.chips.push_back(std::move(c));
    }
    out.rows = out.chips.empty() ? 0 : out.chips.back().row + 1;
}

// Where the sections go under the preview: the left edge, the next free y, the bottom of the card
// and a row's height.
struct SectionsSlot {
    float x;
    float y;
    float bottom;
    float row_h;
};

// The sections as chips with their bars, while there is room; a tap opens the song looping that
// section and the result is true. `cy` moves down past the rows drawn.
bool draw_section_chips(Kit& k, ImDrawList* dl, LibraryPort& port, const SongSections* marked,
                        const SectionChips& chips, const SectionsSlot& slot, std::size_t song,
                        float& cy) {
    if (chips.rows == 0 || marked == nullptr) {
        return false;
    }
    const float cs = k.s(tok::kCaption);
    const float room_rows =
        (slot.bottom - k.s(tok::kS1) - (slot.y + k.s(tok::kS3) + cs * 1.6f)) / slot.row_h;
    if (room_rows < 1.0f) {
        return false;
    }
    cy += k.s(tok::kS3);
    text_at(k, dl, k.bold, cs, {slot.x, cy}, col::kText3, "Sections");
    cy += cs * 1.6f;
    const int rows = std::min(chips.rows, static_cast<int>(room_rows));
    for (const SectionChips::Chip& c : chips.chips) {
        if (c.row >= rows) {
            break;
        }
        const Section& sec = marked->sections[c.index];
        ImGui::PushID(static_cast<int>(sec.id));
        const bool tapped =
            text_chip(k, "##details-section", nullptr, c.text.view(), nullptr,
                      {slot.x + c.x, cy + static_cast<float>(c.row) * slot.row_h}, c.w, false);
        ImGui::PopID();
        const LoopSpan span = tapped ? marked->span_of(sec.id) : LoopSpan{};
        if (span.ok) {
            port.open_at_section(song, span.start_s, span.end_s, static_cast<int>(sec.id));
            return true;
        }
    }
    cy += static_cast<float>(rows) * slot.row_h;
    return false;
}

// The best runs, one line each, while there is room.
void draw_best_runs(const Kit& k, ImDrawList* dl, const std::vector<BestEntry>& bests, float tx,
                    float right, float cy, float bottom) {
    if (bests.empty()) {
        return;
    }
    const float cs = k.s(tok::kCaption);
    if (cy + cs * 1.6f > bottom - k.s(tok::kS3)) {
        return;
    }
    text_at(k, dl, k.bold, cs, {tx, cy}, col::kText3, "Best runs");
    cy += cs * 1.6f;
    for (const BestEntry& b : bests) {
        const float line = k.s(tok::kLabel) * 1.7f;
        if (cy + line > bottom - k.s(tok::kS3)) {
            break;
        }
        const float size = k.s(tok::kLabel);
        const UnitText pct = format_unit(b.accuracy * 100.0, Unit::Percent, 0);
        text_at(k, dl, k.bold, size, {tx, cy}, col::kText, pct.c_str());
        FitText what;
        what.format("%s, %s, %s", b.mode == RunMode::Wait ? "Wait" : "Flow",
                    b.hands == HandFilter::Both   ? "both hands"
                    : b.hands == HandFilter::Left ? "left hand"
                                                  : "right hand",
                    format_unit(b.speed_percent, Unit::Percent, 0).c_str());
        const FitText what_cut =
            fit(k.regular, size, what.view(), right - (tx + k.s(tok::kS6) + k.s(tok::kS4)));
        text_at(k, dl, k.regular, size, {tx + k.s(tok::kS6) + k.s(tok::kS4), cy}, col::kText2,
                what_cut.c_str());
        cy += line;
    }
}

// The song's title lines (`lines`), then its composer and genre and, for a downloaded song, where it
// is from; the text runs from `tx` to `right`, the origin line is `origin_h` tall. `cy` moves past all.
void draw_details_head(Kit& k, ImDrawList* dl, const CatalogueSong& s, const SongOrigin* origin,
                       const TitleLines& lines, float title_size, float tx, float right,
                       float origin_h, float& cy) {
    for (int i = 0; i < lines.count; ++i) {
        text_at(k, dl, k.bold, title_size, {tx, cy}, col::kText, lines.line[i].c_str());
        cy += title_size * 1.25f;
    }
    {
        FitText sub;
        sub.assign(s.composer.empty() ? std::string_view("Composer not known")
                                      : std::string_view(s.composer));
        sub.append(" \xC2\xB7 ");
        sub.append(s.genre.empty() ? std::string_view("Other") : std::string_view(s.genre));
        const float size = k.s(tok::kLabel);
        const FitText cut = fit(k.regular, size, sub.view(), right - tx);
        text_at(k, dl, k.regular, size, {tx, cy}, col::kText2, cut.c_str());
        cy += size * 1.4f;
        if (origin != nullptr) {
            CatalogueId id{};
            FitText from;
            if (catalogue_from_key(origin->catalogue, id)) {
                from.assign("From ");
                from.append(catalogue_info(id).name);
                from.append(" \xC2\xB7 ");
            }
            from.append(origin->licence);
            if (!origin->credit.empty()) {
                from.append(" \xC2\xB7 ");
                from.append(origin->credit);
            }
            const float cs = k.s(tok::kCaption);
            const FitText from_cut = fit(k.regular, cs, from.view(), right - tx);
            label_at(k, dl, k.regular, cs, {tx, cy, right, cy + origin_h}, LabelAlign::Left,
                     col::kText3, from_cut.c_str());
            cy += origin_h;
        }
        cy += k.s(tok::kS2);
    }
}

} // namespace

// The chips of the song's sections, planned again only when the song, its sections, the width or
// the scale changed (the plan cuts and measures words, which allocates).
const SectionChips& LibraryScreen::section_chips(const Kit& k, const SongSections* marked,
                                                 float width) {
    const SectionChipsKey key{marked != nullptr ? marked->song_hash : 0,
                              marked != nullptr ? marked->sections.size() : 0,
                              marked != nullptr ? marked->last_used : 0, width, k.u};
    if (!(key == section_chips_key_)) {
        section_chips_key_ = key;
        plan_section_chips(k, marked, width, section_chips_);
    }
    return section_chips_;
}

void LibraryScreen::draw_details(Kit& k, UiModel& model, LibraryPort& port, const Box& area) {
    if (over_song_ >= port.catalogue().size()) {
        over_ = Over::None;
        return;
    }
    const CatalogueSong& s = port.catalogue().song(over_song_);
    const float margin = k.s(lib_tok::kSidePad);
    const float w = std::min(k.s(lib_tok::kDetailsW), area.w() - 2.0f * margin);
    const float pad = k.s(tok::kS5);
    const float hit = k.s(tok::kHit);
    const float title_size = k.s(tok::kTitle);
    const float gap = k.s(tok::kS2);
    const float most_h = std::min(k.s(lib_tok::kDetailsH), area.h() - 2.0f * margin);
    // The actions (Favourite, Groups, Rename, Composer, Add to Melodies) stack on the right, under
    // the close button, while the left keeps room for the song and the card is tall enough; else
    // they sit in one row beside Play, as icons when their words do not fit. Preview is always in
    // that row, right after Play, at Play's height (`kPreviewAt` is its place in `actions`).
    constexpr int kStacked = 6;
    constexpr int kPreviewAt = 4;
    constexpr int kInStack = kStacked - 1;
    const float stack_w = k.s(lib_tok::kDetailsStackW);
    const float stack_h = 2.0f * gap + static_cast<float>(kInStack + 1) * hit +
                          static_cast<float>(kInStack - 1) * k.s(tok::kHitGap) + pad;
    // While the preview plays, the stack leaves it its full height or goes into the row.
    const float preview_full = previewing_ ? k.s(tok::kS4) + k.s(lib_tok::kPreviewH) : 0.0f;
    const bool side = w - 2.0f * pad - stack_w - k.s(tok::kS4) >= k.s(lib_tok::kDetailsLeastLeftW) &&
                      stack_h + preview_full <= most_h;
    const float left_w = side ? w - 2.0f * pad - stack_w - k.s(tok::kS4) : w - 2.0f * pad;
    const TitleLines& lines =
        side ? title_lines(k.bold, title_size, s.title, left_w, left_w)
             : title_lines(k.bold, title_size, s.title, w - 2.0f * pad - hit, w - 2.0f * pad);
    // As tall as what it holds: the title, the composer line, the facts, the score, Play (and the
    // row of actions beside it), the preview while it plays and the best runs (as many as fit);
    // with the stack, at least that.
    const float label = k.s(tok::kLabel);
    // The facts row and the score row; a card too short for them and the preview (800x480 at a
    // large scale) leaves them out while the preview plays.
    const float facts_h = label * 1.4f + k.s(tok::kS4);
    // The player's practice choices for the song (its hands and speed), one row under the facts
    // when the history holds any.
    const SongPractice& practice = port.practice(over_song_);
    const float practice_h = label * 1.4f + k.s(tok::kS3);
    const float score_h = k.s(tok::kFace) + k.s(tok::kS4);
    // A song downloaded from a catalogue: where from, its licence and the credit it asks for.
    const SongOrigin* origin = port.origin(over_song_);
    const float origin_h = origin != nullptr ? k.s(tok::kCaption) * 1.4f : 0.0f;
    // The versions of the piece, as chips on as many rows as they need.
    const bool versioned = !port.catalogue().versions_of(over_song_).empty();
    const auto vchips =
        measure_version_chips(chip_widths_, k, port.catalogue(), over_song_, left_w, true);
    const auto vrows = static_cast<float>(vchips.plan.rows); // a dead gap apart, then the gap under
    const float versions_h = versioned ? vrows * hit + (vrows - 1.0f) * k.s(tok::kHitGap) + gap : 0.0f;
    // The two actions about the piece (#213): taking the song out of its piece, or having the
    // files group it again, and "Same piece as another song". Chips in a row (two when they do not fit).
    const LibraryPort::VersionSaid said = port.version_said(over_song_);
    const bool piece_actions = s.hash != 0;
    const char* not_text = said == LibraryPort::VersionSaid::Split ? "Group automatically again"
                           : versioned                               ? "Not a version of this piece"
                           : said == LibraryPort::VersionSaid::Joined ? "Group automatically again"
                                                                      : nullptr;
    const char* not_glyph = not_text != nullptr && not_text[0] == 'G' ? icon::kUndo : icon::kClose;
    if (piece_w_.unit != k.u) {
        piece_w_.unit = k.u;
        piece_w_.not_version = chip_width(k, icon::kClose, "Not a version of this piece", nullptr);
        piece_w_.again = chip_width(k, icon::kUndo, "Group automatically again", nullptr);
        piece_w_.same = chip_width(k, icon::kMerge, "Same piece as another song", nullptr);
    }
    const float not_w = not_text == nullptr ? 0.0f
                        : not_text[0] == 'G' ? piece_w_.again
                                             : piece_w_.not_version;
    const int piece_rows =
        !piece_actions ? 0
                       : (not_text != nullptr && not_w + gap + piece_w_.same > left_w ? 2 : 1);
    // Two rows of chips stand a dead gap apart.
    const float piece_h = piece_rows > 0 ? static_cast<float>(piece_rows) * hit +
                                               static_cast<float>(piece_rows - 1) * k.s(tok::kHitGap) + gap
                                         : 0.0f;
    float fixed = pad * 2.0f + title_size * 1.25f * static_cast<float>(lines.count) +
                  label * 1.4f + origin_h + gap + versions_h + piece_h + facts_h + practice_h +
                  score_h + hit;
    const float preview_want = previewing_ ? k.s(tok::kS4) + k.s(lib_tok::kPreviewH) : 0.0f;
    // A card too tall for the window (the small windows at a large scale, with the chips of
    // several versions) leaves out the facts and the score; if it is still too tall, the version
    // chips too (the card holds them): Play and the actions must stay on it.
    const bool squeeze = (previewing_ && std::max(fixed, side ? stack_h : 0.0f) + k.s(tok::kS4) +
                                                 k.s(lib_tok::kPreviewLeastH) >
                                             most_h) ||
                         fixed > most_h;
    if (squeeze) {
        fixed -= facts_h + practice_h + score_h;
    }
    const bool chips_hidden = versioned && squeeze && fixed > most_h;
    if (chips_hidden) {
        fixed -= versions_h;
    }
    const float runs_h = bests_.empty() ? 0.0f
                                        : k.s(tok::kS4) + k.s(tok::kCaption) * 1.6f +
                                              static_cast<float>(bests_.size()) * label * 1.7f;
    // The song's sections as chips under Play, in at most kSectionRows rows.
    const SongSections* marked = s.hash != 0 ? model.sections.find(s.hash) : nullptr;
    const SectionChips& chips = section_chips(k, marked, w - 2.0f * pad);
    const float sections_h =
        chips.rows == 0 ? 0.0f
                        : k.s(tok::kS3) + k.s(tok::kCaption) * 1.6f +
                              static_cast<float>(chips.rows) * hit;
    // The preview, the sections and the runs go under the taller of the song's column and the
    // stack.
    const float h = std::min(
        most_h, std::max(fixed, side ? stack_h : 0.0f) + preview_want + sections_h + runs_h);
    const float x = area.cx() - w * 0.5f;
    const float y = area.y0 + std::max(margin, (area.h() - h) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    sheet_card(k, dl, {x, y}, {x + w, y + h}, k.s(tok::kRadiusSheet), col::kSurface);
    k.toast_avoid({x, y, w, h});
    // The close button in the top corner, as every surface closes.
    if (round_button(k, "##details-close", icon::kClose,
                     {x + w - k.s(tok::kS2) - hit * 0.5f, y + k.s(tok::kS2) + hit * 0.5f}, false,
                     Idle::Fill)) {
        over_ = Over::None;
        return;
    }
    float cy = y + pad;
    const float tx = x + pad;
    const float right = tx + left_w;
    const bool known = s.hash != 0;
    const bool in_melodies = known && melody_list(model.app).step_of(s.hash, s.relative) != 0;
    // The actions, in the stack or the row: what each does.
    struct DetailAction {
        const char* id;
        const char* glyph_text;
        const char* text;
        bool on;
    };
    const DetailAction actions[kStacked] = {
        {"##details-heart", lib_icon::kHeart, "Favourite", s.piece_favourite},
        {"##details-groups", lib_icon::kAddTo, "Groups", false},
        {"##details-rename", icon::kRename, "Rename", false},
        {"##details-composer", lib_icon::kUser, "Composer", false},
        {"##details-preview", lib_icon::kPreview, "Preview", previewing_},
        {"##details-melody", icon::kMelodies, in_melodies ? "In Melodies" : "Add to Melodies", in_melodies},
    };
    bool leave = false;
    const auto act = [&](int n, ImVec2 at_pos, ImVec2 size) {
        switch (n) {
        case 0:
            if (known) {
                toggle_favourite(k, port, over_song_);
            }
            break;
        case 1:
            if (known) {
                open_for(Over::AddToGroup, false, {at_pos.x + size.x, at_pos.y + size.y});
                leave = true;
            }
            break;
        case 2:
            if (known) {
                start_name(port, over_song_, Edit::Rename);
                leave = true;
            }
            break;
        case 3:
            if (known) {
                start_name(port, over_song_, Edit::Composer);
                leave = true;
            }
            break;
        case 4:
            if (previewing_) {
                stop_preview(k, port);
            } else {
                previewing_ = true;
                preview_sound_ = false; // muted until asked
                port.request_preview(over_song_);
            }
            break;
        case 5:
            if (known) {
                open_melody_pick(model.app, s);
                leave = true;
            }
            break;
        default:
            break;
        }
    };
    float stack_bottom = y;
    if (side) {
        float sy = y + gap + hit + gap;
        const float sx = x + w - pad - stack_w;
        for (int n = 0; n < kStacked && !leave; ++n) {
            if (n == kPreviewAt) {
                continue;
            }
            const DetailAction& a = actions[n];
            // Each row on whole pixels: the gap between two (tok::kHitGap at a scale of 1.5) is
            // fractional, and a row at a half pixel puts its icon off the button's centre.
            const float row_y = std::round(sy);
            if (text_chip(k, a.id, a.glyph_text, a.text, nullptr, {sx, row_y}, stack_w, a.on)) {
                act(n, {sx, row_y}, {stack_w, hit});
            }
            sy += hit + k.s(tok::kHitGap);
        }
        stack_bottom = sy;
        if (leave) {
            return;
        }
    }
    // The title (two lines at most), the composer and the genre.
    draw_details_head(k, dl, s, origin, lines, title_size, tx, right, origin_h, cy);
    // The versions: a tap on one shows that version here (and the card of the piece opens it).
    if (versioned && !chips_hidden) {
        const float rows_h = versions_h - gap;
        if (const std::optional<std::size_t> tapped =
                draw_version_chips(k, port, over_song_, {tx, cy}, left_w, true, true)) {
            if (previewing_) {
                stop_preview(k, port); // it played the other version
            }
            open_details(port, *tapped);
            return;
        }
        cy += rows_h + gap;
    }
    // The piece: out of it or grouped again, and "Same piece as another song".
    if (piece_rows > 0) {
        float px = tx;
        float py = cy;
        if (not_text != nullptr) {
            if (text_chip(k, "##details-not-version", not_glyph, not_text, nullptr, {px, py}, not_w,
                          false)) {
                if (not_text[0] == 'G') {
                    port.reset_versions(over_song_);
                    k.say_free("Grouped by the files again");
                } else {
                    port.split_version(over_song_);
                    k.say_free("Taken out of its piece");
                }
            }
            px += not_w + gap;
            if (piece_rows == 2) {
                px = tx;
                py += hit + k.s(tok::kHitGap);
            }
        }
        if (text_chip(k, "##details-same", icon::kMerge, "Same piece as another song", nullptr,
                      {px, py}, piece_w_.same, false)) {
            // The search is where the other song is found; the next tap on a card joins them.
            joining_ = over_song_;
            joining_hash_ = s.hash;
            stop_selecting();
            over_ = Over::None;
            edit_ = Edit::Search;
            keys_ = by_touch();
            query_.clear();
            return;
        }
        cy += piece_h;
    }
    // The facts and the practice choices, then the best score and the runs.
    if (!squeeze) {
        draw_details_facts(k, dl, s, practice, tx, cy);
        draw_details_score(k, dl, port, s, tx, right, cy);
    }
    // Play with Preview right after it, and in the same row the actions the stack holds when there
    // is no room for it.
    {
        const float bh = hit;
        const DetailAction& preview = actions[kPreviewAt];
        const auto words_w = [&](const DetailAction& a) {
            return chip_width(k, a.glyph_text, a.text, nullptr);
        };
        // After Play, in the row: Preview and, with no stack, the other actions. Preview and the
        // others keep their words while Play still has its least width, else the others are icons a
        // hit wide, and last Preview too.
        const float play_least = chip_width(k, icon::kPlay, "Play", nullptr);
        float others_words = 0.0f;
        for (int n = 0; n < kStacked && !side; ++n) {
            others_words += n == kPreviewAt ? 0.0f : gap + words_w(actions[n]);
        }
        const float after_room = left_w - play_least;
        const bool other_words = side || gap + words_w(preview) + others_words <= after_room;
        const bool preview_words =
            other_words || gap + words_w(preview) + kInStack * (gap + hit) <= after_room;
        const float preview_w = preview_words ? words_w(preview) : hit;
        float after = gap + preview_w;
        for (int n = 0; n < kStacked && !side; ++n) {
            after += n == kPreviewAt ? 0.0f : gap + (other_words ? words_w(actions[n]) : hit);
        }
        const float nominal =
            side ? std::min(left_w, std::max(k.s(lib_tok::kPopoverW) * 0.5f, left_w * 0.5f))
                 : std::max(k.s(lib_tok::kPopoverW) * 0.5f, (right - tx) * 0.3f);
        // Play gives way before the others do, down to its least width.
        const float play_w = std::min(nominal, std::max(play_least, left_w - after));
        // The row on whole pixels (what is above it ends at a fraction), so its icons sit centred.
        const float row_y = std::round(cy);
        if (action_button(k, "##details-play", icon::kPlay, "Play", {tx, row_y}, {play_w, bh}, true,
                          true)) {
            port.open(over_song_);
            over_ = Over::None;
            return;
        }
        float bx = tx + play_w + gap;
        if (text_chip(k, preview.id, preview.glyph_text, preview_words ? preview.text : "", nullptr,
                      {bx, row_y}, preview_w, preview.on)) {
            act(kPreviewAt, {bx, row_y}, {preview_w, bh});
        }
        long_press_tip(k, preview.text);
        bx += preview_w + gap;
        for (int n = 0; n < kStacked && !side && !leave; ++n) {
            if (n == kPreviewAt) {
                continue;
            }
            const DetailAction& a = actions[n];
            const float cw = other_words ? words_w(a) : hit;
            if (text_chip(k, a.id, a.glyph_text, other_words ? a.text : "", nullptr, {bx, row_y}, cw,
                          a.on)) {
                act(n, {bx, row_y}, {cw, bh});
            }
            long_press_tip(k, a.text);
            bx += cw + gap;
        }
        if (leave) {
            return;
        }
        cy += bh + k.s(tok::kS4);
    }
    // The opening bars, falling, under everything above (and the stack), across the card.
    if (previewing_) {
        const float py = std::max(cy, stack_bottom + gap);
        const float room = y + h - pad - py;
        const float ph = std::min(k.s(lib_tok::kPreviewH), room);
        if (ph < hit) {
            return; // no room at all (a window smaller than any the app is made for)
        }
        draw_preview(k, model, port, {tx, py}, w - 2.0f * pad, ph);
        cy = py + ph + k.s(tok::kS4);
    }
    if (draw_section_chips(k, dl, port, marked, chips, {tx, cy, y + h, hit}, over_song_, cy)) {
        over_ = Over::None;
        return;
    }
    draw_best_runs(k, dl, bests_, tx, right, cy, y + h);
}

void LibraryScreen::draw_preview(Kit& k, UiModel& model, LibraryPort& port, ImVec2 pos, float w,
                                 float h, std::string_view waiting) {
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The sound (off at first) and the sheet music switch beside the picture: one over the other
    // where it is tall enough, else side by side.
    const bool stacked = h >= 2.0f * hit + gap;
    const float pw = w - (stacked ? hit + gap : 2.0f * (hit + gap));
    const ImVec2 sound_c{pos.x + w - hit * 0.5f, pos.y + hit * 0.5f};
    const ImVec2 sheet_c = stacked ? ImVec2{sound_c.x, sound_c.y + hit + gap}
                                   : ImVec2{sound_c.x - hit - gap, sound_c.y};
    if (round_button(k, "##details-sound", preview_sound_ ? icon::kSoundBoth : icon::kMute, sound_c,
                     preview_sound_, Idle::Fill)) {
        preview_sound_ = !preview_sound_;
        if (!preview_sound_ && k.library_preview != nullptr) {
            // What is sounding stops; the picture plays on.
            const std::bitset<128>& on = k.library_preview->sounding();
            for (std::size_t key = 0; key < on.size(); ++key) {
                if (on.test(key)) {
                    port.preview_note(static_cast<std::uint8_t>(key), 0);
                }
            }
        }
    }
    long_press_tip(k, preview_sound_ ? "Sound off" : "Sound on");
    if (round_button(k, "##details-sheet", lib_icon::kSheet, sheet_c, preview_sheet_, Idle::Fill)) {
        preview_sheet_ = !preview_sheet_;
    }
    long_press_tip(k, preview_sheet_ ? "Falling notes" : "Sheet music");
    const SongOpening* opening = port.preview();
    const float radius = k.s(tok::kRadiusMd);
    const auto message = [&](std::string_view line, bool paper) {
        if (!paper) {
            dl->AddRectFilled(pos, {pos.x + pw, pos.y + h}, u32(col::kFill), radius);
        }
        const float ls = k.s(tok::kLabel);
        const FitText cut = fit(k.regular, ls, line, pw - 2.0f * gap);
        label_at(k, dl, k.regular, ls, {pos.x, pos.y, pos.x + pw, pos.y + h}, LabelAlign::Centre,
                 paper ? ImVec4{0.25f, 0.25f, 0.28f, 1.0f} : col::kText2, cut.c_str());
    };
    if (opening == nullptr || k.library_preview == nullptr) {
        message(!waiting.empty()        ? waiting
                : port.preview_failed() ? std::string_view("No notes to show")
                                        : std::string_view("Reading the opening\xE2\x80\xA6"),
                false);
        return;
    }
    LivePreview& lp = *k.library_preview;
    lp.set_opening(opening, port.preview_keys());
    if (preview_sheet_ && k.library_sheet != nullptr) {
        // The loop runs on unseen, for its sound and its place; the score shows where it is.
        const double t = lp.run_opening(k.now);
        SheetPreview& sheet = *k.library_sheet;
        const float space =
            std::max(k.s(lib_tok::kPreviewSheetLeastSpace), h / lib_tok::kPreviewSheetSpaces);
        sheet.want(port.preview_song(), (pw - 2.0f * gap) * k.fb_scale, space * k.fb_scale);
        sheet.update();
        audit::live(pos, {pos.x + pw, pos.y + h});
        sheet.draw(dl, pos, {pos.x + pw, pos.y + h}, t, radius, 1.0f / k.fb_scale);
        if (sheet.status() == SheetPreview::Status::NoSheet) {
            FitText why;
            why.format("No sheet music: %.*s", static_cast<int>(sheet.reason().size()),
                       sheet.reason().data());
            message(why.view(), true);
        } else if (sheet.status() != SheetPreview::Status::Ready) {
            message("Engraving the score\xE2\x80\xA6", true);
        }
    } else {
        ImGui::SetCursorScreenPos(pos);
        lp.draw(k, model.look, pw, h, PreviewLoop::Opening);
    }
    for (const LivePreview::Struck& s : lp.struck()) {
        if (preview_sound_ || s.velocity == 0) {
            port.preview_note(s.key, s.velocity);
        }
    }
    lp.clear_struck();
}

void LibraryScreen::draw_name(Kit& k, LibraryPort& port, const Box& area, float keys_top) {
    const bool song_field = edit_ == Edit::Rename || edit_ == Edit::Composer;
    if (song_field && over_song_ >= port.catalogue().size()) {
        over_ = Over::None;
        edit_ = Edit::None;
        return;
    }
    osk::NameCard card;
    card.top = true;
    card.width = lib_tok::kDetailsW;
    // A piece with versions: the card names the version it changes.
    FitText title;
    const std::string_view said =
        song_field ? port.catalogue().version_name(over_song_) : std::string_view();
    switch (edit_) {
    case Edit::Rename:
    case Edit::Composer: {
        const char* what = edit_ == Edit::Rename ? "Rename" : "Composer";
        if (said.empty()) {
            card.title = what;
        } else {
            title.format("%s: the %.*s version", what, static_cast<int>(said.size()), said.data());
            card.title = title.c_str();
        }
        break;
    }
    case Edit::NewGroup:
        card.title = "New group";
        break;
    case Edit::GroupName:
        card.title = "Rename the group";
        break;
    case Edit::NewFolder:
        card.title = "New folder";
        break;
    case Edit::None:
    case Edit::Search:
        break;
    }
    std::string_view own;
    if (song_field) {
        const CatalogueSong& s = port.catalogue().song(over_song_);
        own = edit_ == Edit::Rename ? std::string_view(s.file_title)
                                    : std::string_view(s.file_composer);
        card.own = "The file's";
        card.own_enabled = name_ != own;
    }
    card.can_save = song_field || !name_.empty();
    const osk::NameDrawn drawn = osk::draw_name_card(k, area, keys_top, name_, card);
    if (drawn.field_tapped) {
        keys_ = by_touch();
        keyboard_.reset();
    }
    const bool done = done_name_;
    done_name_ = false;
    if (drawn.result == osk::NameResult::Own) {
        name_.assign(own);
    } else if (drawn.result == osk::NameResult::Cancel) {
        edit_ = Edit::None;
        over_ = name_back_;
    } else if ((drawn.result == osk::NameResult::Save || done) && card.can_save) {
        finish_edit(k, port, true);
    }
}

} // namespace etude::app::ui
