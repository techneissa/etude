// SPDX-License-Identifier: GPL-3.0-or-later
// The Melodies page of the Learn screen (learn_melodies.hpp): the sections, the tiles and their
// touch. The picture of a tile's border and ring is in learn_melodies_art.cpp.
#include "learn_melodies.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "learn_melodies_art.hpp"
#include "learn_screen.hpp"
#include "library_screen_shared.hpp"

#include "etude/learn_colours.hpp"
#include "etude/melody_progress.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <span>

namespace etude::app::ui {

namespace {

ImVec4 ink_of(RGBA c) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, 1.0f};
}

// The icon a path wears by the name the manifest gives it (the parser refuses any other).
const char* path_icon(const std::string& name) {
    if (name == "sprout") {
        return icon::kSprout;
    }
    return name == "handshake" ? icon::kHandshake : icon::kAward;
}

// What the path's button says: Start (nothing begun), Continue, or Again (all passed).
constexpr std::array<const char*, 3> kButtonWords{"Start", "Continue", "Again"};
std::size_t button_kind(const MelodyPathProgress& p) {
    if (p.next < 0) {
        return 2;
    }
    return p.started ? 1 : 0;
}

Box clipped(const Box& a, const Box& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

bool shows(const Box& b) { return b.w() > 0.0f && b.h() > 0.0f; }

// The badge: the number in a ring (a library song's step in a rounded square), or a green disc with a
// check once passed.
void draw_badge(Kit& k, const Box& badge, const char* id, bool passed, bool next, bool library,
                int number, RGBA colour) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 bc{badge.cx(), badge.cy()};
    const float br = badge.w() * 0.5f;
    if (passed) {
        dl->AddCircleFilled(bc, br, u32(ink_of(melody_fill_colour(1.0f))), 32);
        glyph(k, dl, icon::kCheck, bc, k.s(tok::kIconSm), col::kAccentInk);
        audit::note(id, {badge.x0, badge.y0}, {badge.x1, badge.y1}, InkKind::Centred);
        return;
    }
    const ImVec4 ink = next || library ? ink_of(colour) : col::kText3;
    if (library) {
        dl->AddRect({badge.x0, badge.y0}, {badge.x1, badge.y1}, u32(ink), k.s(tok::kRadiusControl), 0,
                    k.s(tok::kHairline) * 1.5f);
    } else {
        dl->AddCircle(bc, br - k.s(tok::kHairline), u32(ink), 32, k.s(tok::kHairline) * 1.5f);
    }
    char text[16];
    std::snprintf(text, sizeof text, "%d", number);
    label_at(k, dl, k.bold, k.s(tok::kLabel), badge, LabelAlign::Centre,
             next || library ? ink_of(colour) : col::kText2, text);
}

} // namespace

MelodiesView::MelodiesView() { page_drag_.set_axis(ScrollDrag::Axis::Vertical, true); }

bool MelodiesView::moving() const {
    return page_drag_.busy() || std::any_of(strips_.begin(), strips_.end(),
                                            [](const ChipStrip& s) { return s.busy(); });
}

void MelodiesView::cancel() {
    page_drag_.cancel();
    for (ChipStrip& s : strips_) {
        s.cancel();
    }
}

// ---- Touch --------------------------------------------------------------------------------------

// The pointer on the page: a drag or a wheel moves it down and up, and a touch that became a scroll
// is no tap on the tile under it.
void MelodiesView::page_pointer(Kit& k, const Box& body, float max_scroll_pt) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         body.holds({io.MousePos.x, io.MousePos.y, io.MousePos.x, io.MousePos.y});
    const float px = io.MousePos.x / k.u;
    const float py = io.MousePos.y / k.u;
    const float max_dp = max_scroll_pt / k.u;
    if (ImGui::IsMouseClicked(0) && hovered) {
        page_drag_.press(px, py, k.now, io.MouseSource != ImGuiMouseSource_Mouse);
    }
    if (hovered && io.MouseWheel != 0.0f) {
        scroll_ = std::clamp(scroll_ - io.MouseWheel * learn_tok::kStripWheel, 0.0f, max_dp);
    }
    if (page_drag_.busy()) {
        if (ImGui::IsMouseDown(0)) {
            page_drag_.move(px, py, k.now);
        } else {
            page_drag_.release(px, py, k.now);
        }
    }
    const ScrollDrag::Step step = page_drag_.update(k.now, false, scroll_, max_dp);
    scroll_ = std::clamp(scroll_ + step.scroll_by, 0.0f, max_dp);
    if (step.took_touch) {
        ImGui::ClearActiveID();
    }
}

// The pointer on one path's row: a press on it may become a sideways scroll (the tile under it lets
// go), and the wheel's sideways turn scrolls it.
void MelodiesView::row_pointer(Kit& k, std::size_t path, const Box& view) {
    ChipStrip& strip = strips_[path];
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         view.holds({io.MousePos.x, io.MousePos.y, io.MousePos.x, io.MousePos.y});
    const float px = io.MousePos.x / k.u;
    const float py = io.MousePos.y / k.u;
    if (ImGui::IsMouseClicked(0) && hovered) {
        strip.press(px, py, k.now, io.MouseSource != ImGuiMouseSource_Mouse);
    }
    if (hovered && io.MouseWheelH != 0.0f) {
        strip.scroll_by(-io.MouseWheelH * learn_tok::kStripWheel);
    }
    if (strip.busy()) {
        if (ImGui::IsMouseDown(0)) {
            strip.move(px, py, k.now);
        } else {
            strip.release(px, py, k.now);
        }
    }
    if (strip.update(k.now)) {
        ImGui::ClearActiveID();
    }
}

MelodiesLayout MelodiesView::begin(Kit& k, const Box& area, const MelodyNeeds& needs) {
    const MelodiesLayout first = melodies_layout(area, k.u, needs, scroll_ * k.u);
    page_pointer(k, first.body, first.max_scroll);
    return melodies_layout(area, k.u, needs, scroll_ * k.u);
}

// ---- Titles -------------------------------------------------------------------------------------

// Each title as one line, or two where it is wider than `width`: as many words as fit on the first,
// the rest on the second, cut short with an ellipsis if it is still too wide. Made once for a width
// and size, never in a frame that changed neither.
void MelodiesView::wrap_titles(Kit& k, const std::vector<MelodyPiece>& pieces, std::vector<Title>& out,
                               float width, float size, bool two_lines) {
    out.assign(pieces.size(), Title{});
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const std::string& whole = pieces[i].title;
        Title& t = out[i];
        // A tagged piece's pill takes its place after the title, so a second line gives it room.
        float room = width;
        if (!pieces[i].tag.empty() && two_lines) {
            t.tag_w = label_width(k.regular, k.s(melody_tok::kTagPt), pieces[i].tag.c_str()) + 2.0f * k.s(tok::kS2);
            room = width - t.tag_w - k.s(tok::kS1);
        }
        if (text_size(k.bold, size, whole.c_str()).x <= width) {
            t.line[0].assign(whole);
            continue;
        }
        if (!two_lines) {
            t.line[0] = fit(k.bold, size, whole, width);
            continue;
        }
        // The split between two words that makes the longer line shortest, so "Twinkle Twinkle / Little
        // Star" and not "Twinkle Twinkle Little / Star".
        std::size_t cut = 0;
        float best = 0.0f;
        for (std::size_t at = whole.find(' '); at != std::string::npos; at = whole.find(' ', at + 1)) {
            const float a = text_size(k.bold, size, whole.substr(0, at).c_str()).x;
            const float b = text_size(k.bold, size, whole.substr(at + 1).c_str()).x;
            const float longer = std::max(a, b);
            if (a <= width && (cut == 0 || longer < best)) {
                cut = at;
                best = longer;
            }
        }
        if (cut == 0) {
            t.line[0] = fit(k.bold, size, whole, width);
            continue;
        }
        t.lines = 2;
        t.line[0].assign(whole.substr(0, cut));
        t.line[1] = fit(k.bold, size, std::string_view(whole).substr(cut + 1), std::min(width, room));
        t.second_w = text_size(k.bold, size, t.line[1].c_str()).x;
    }
}

// ---- Drawing ------------------------------------------------------------------------------------

void MelodiesView::draw_title(Kit& k, const MelodyPiece& piece, const Title& title, const MelodyTileBoxes& t,
                              float size, ImVec4 ink, bool library_short) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box top{t.title.x0, t.title.y0, t.title.x1, t.title.cy()};
    const Box bottom{t.title.x0, t.title.cy(), t.title.x1, t.title.y1};
    const bool tagged = title.tag_w > 0.0f;
    if (library_short) {
        label_at(k, dl, k.bold, size, top, LabelAlign::Left, ink, title.line[0].c_str());
        const FitText detail = fit(k.regular, k.s(tok::kCaption), piece.goal, bottom.w());
        label_at(k, dl, k.regular, k.s(tok::kCaption), bottom, LabelAlign::Left, col::kText2, detail.c_str());
        return;
    }
    if (title.lines == 2) {
        label_at(k, dl, k.bold, size, top, LabelAlign::Left, ink, title.line[0].c_str());
        label_at(k, dl, k.bold, size, bottom, LabelAlign::Left, ink, title.line[1].c_str());
    } else {
        label_at(k, dl, k.bold, size, tagged ? top : t.title, LabelAlign::Left, ink, title.line[0].c_str());
    }
    if (!tagged) {
        return;
    }
    // The pill: a plain muted face with the tag in it, in the second line's room.
    const float h = std::min(k.s(melody_tok::kTagH), bottom.h());
    const float x = bottom.x0 + (title.lines == 2 ? title.second_w + k.s(tok::kS1) : 0.0f);
    const Box pill{x, bottom.cy() - h * 0.5f, x + title.tag_w, bottom.cy() + h * 0.5f};
    dl->AddRectFilled({pill.x0, pill.y0}, {pill.x1, pill.y1}, u32(col::kFill), h * 0.5f);
    label_at(k, dl, k.regular, k.s(melody_tok::kTagPt), {pill.x0, pill.y0, pill.x1, pill.y1}, LabelAlign::Centre,
             col::kText2, piece.tag.c_str());
}

void MelodiesView::draw_hands(Kit& k, const MelodyPiece& piece, const MelodyTileBoxes& t) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto one = [&](const Box& cell, const char* glyph_text, const char* mark, ImVec4 colour, int keys,
                         const char* id) {
        if (!shows(cell)) {
            return;
        }
        const float icon_h = k.s(melody_tok::kHandIcon);
        const Box icon_box{cell.x0, cell.y0, cell.x1, cell.y0 + icon_h};
        glyph(k, dl, glyph_text, {cell.cx(), icon_box.cy()}, icon_h, colour);
        audit::note(id, {icon_box.x0, icon_box.y0}, {icon_box.x1, icon_box.y1}, InkKind::Centred, false);
        char count[16];
        std::snprintf(count, sizeof count, "%d", keys);
        const Box slot{cell.x0 - k.s(tok::kS1), icon_box.y1 + k.s(tok::kS1), cell.x1 + k.s(tok::kS1),
                       cell.y1};
        label_at(k, dl, k.regular, k.s(melody_tok::kCountPt), slot, LabelAlign::Centre, col::kText2,
                 count);
        // The hand's letter at the lower right of the glyph.
        const float m = k.s(melody_tok::kHandMark);
        const Box letter{icon_box.cx() + icon_h * 0.18f, icon_box.y1 - m * 0.9f, icon_box.cx() + icon_h * 0.18f + m,
                         icon_box.y1 + m * 0.1f};
        label_at(k, dl, k.bold, m, letter, LabelAlign::Centre, colour, mark);
    };
    one(t.hand_left, icon::kLeftHand, "L", k.hand_left, piece.shape.left.keys(), "##melody-left");
    one(t.hand_right, icon::kRightHand, "R", k.hand_right, piece.shape.right.keys(), "##melody-right");
}

void MelodiesView::draw_play(Kit& k, const MelodyTileBoxes& t, const char* id, MelodyMark mark,
                             float share) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c{t.play.cx(), t.play.cy()};
    const float disc = k.s(melody_tok::kPlay) * 0.5f;
    const float thick = k.s(melody_tok::kRing);
    const float ring = disc + k.s(melody_tok::kPlayGap) + thick * 0.5f;
    dl->AddCircleFilled(c, disc, u32(col::kFill), 40);
    melody_draw_ring(dl, c, ring, thick, mark == MelodyMark::New ? 0.0f : share,
                     u32(ink_of(melody_fill_colour(share))), u32(col::kHairline));
    glyph(k, dl, icon::kPlay, c, k.s(tok::kIconSm), col::kText);
    char name[48];
    std::snprintf(name, sizeof name, "%s-play", id);
    audit::note(name, {c.x - disc, c.y - disc}, {c.x + disc, c.y + disc}, InkKind::PlayTriangle);
}

void MelodiesView::draw_tile(Kit& k, const MelodyPiece& piece, const Title& title, int place,
                             bool next, const Box& tile, const Box& visible, const MelodiesLayout& lay,
                             const char* id, bool library, bool* pressed_out) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = k.s(tok::kRadiusCard);
    const MelodyMark mark = melody_mark(piece.best_permille);
    const float share = mark == MelodyMark::Passed ? 1.0f : melody_share(piece.best_permille);
    const bool whole = visible.x0 <= tile.x0 + 0.5f && visible.x1 >= tile.x1 - 0.5f &&
                       visible.y0 <= tile.y0 + 0.5f && visible.y1 >= tile.y1 - 0.5f;
    // One pressable, the tile; a tile cut by the row's or the page's edge to less than a hit is only
    // drawn, and takes a tap once it is scrolled in.
    const bool pressable = visible.w() >= k.s(tok::kHit) - 0.5f && visible.h() >= k.s(tok::kHit) - 0.5f;
    dl->AddRectFilled({tile.x0, tile.y0}, {tile.x1, tile.y1}, u32(col::kSurface), radius);
    if (next) {
        const ImVec4 own = ink_of(colour_);
        dl->AddRectFilled({tile.x0, tile.y0}, {tile.x1, tile.y1}, u32(with_alpha(own, tok::kHoverAlpha)),
                          radius);
    }
    bool pressed = false;
    State st;
    if (pressable) {
        pressed = tapped(press_area(k, id, visible));
        st = item_state(k, false);
        draw_state(k, dl, id, whole ? tile : visible, visible, st, Idle::None, radius);
        audit::note(id, {visible.x0, visible.y0}, {visible.x1, visible.y1}, InkKind::Target);
    }
    melody_draw_border(dl, {tile.x0, tile.y0}, {tile.x1, tile.y1}, radius, k.s(melody_tok::kBorder),
                       mark == MelodyMark::New ? 0.0f : share, u32(ink_of(melody_fill_colour(share))),
                       u32(col::kHairline));
    const MelodyTileBoxes t = melody_tile_boxes(tile, lay.goal, (piece.shape.left.used() ? 1 : 0) |
                                                              (piece.shape.right.used() ? 2 : 0),
                                                k.u, library);
    draw_badge(k, t.badge, id, mark == MelodyMark::Passed, next, library, library ? piece.step : place + 1,
               colour_);
    // The title, one or two lines; a library tile with no room for a line under it has the title in
    // one and the composer (else the file's name) in the second half of the title's room.
    const float size = k.s(lay.goal ? tok::kBody : tok::kLabel);
    const ImVec4 ink = state_ink(st, col::kText);
    draw_title(k, piece, title, t, size, ink, library && !lay.goal);
    if (shows(t.goal)) {
        Box words = t.goal;
        if (library) {
            // The composer (else the file's name), after the library's mark.
            const float lib_icon = k.s(tok::kIconSm);
            glyph(k, dl, icon::kLibrary, {t.goal.x0 + lib_icon * 0.5f, t.goal.cy()}, lib_icon, col::kText3);
            words.x0 += lib_icon + k.s(tok::kS1);
        }
        const FitText goal = fit(k.regular, k.s(tok::kCaption), piece.goal, words.w());
        label_at(k, dl, k.regular, k.s(tok::kCaption), words, LabelAlign::Left,
                 next ? ink_of(colour_) : col::kText2, goal.c_str());
    } else if (shows(t.mark)) {
        glyph(k, dl, icon::kLibrary, {t.mark.cx(), t.mark.cy()}, t.mark.w(), col::kText3);
    }
    draw_hands(k, piece, t);
    // The library's difficulty stars: the level's colour, filled, half and empty, at the tile's size.
    const lib_shared::LevelStarLook stars{melody_tok::kStar, melody_tok::kStarGap, melody_tok::kStarLine};
    lib_shared::draw_level_stars(k, dl, t.stars.x0, t.stars.cy(), piece.half_stars, &stars);
    draw_play(k, t, id, mark, share);
    *pressed_out = pressed;
}

// The width of a button's words, measured once for the size they are drawn at.
float MelodiesView::button_words_width(const Kit& k, std::size_t kind) {
    const float size = k.s(tok::kLabel);
    if (std::abs(size - words_size_) > 0.01f) {
        for (std::size_t i = 0; i < kButtonWords.size(); ++i) {
            words_w_[i] = label_width(k.bold, size, kButtonWords[i]);
        }
        words_size_ = size;
    }
    return words_w_[kind];
}

void MelodiesView::draw_head(Kit& k, const MelodyBook& book, std::size_t path_at,
                             const MelodyPathProgress& progress, const MelodySectionBoxes& s,
                             MelodyAsk* asked) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const MelodyPath& path = book.paths[path_at];
    const ImVec4 own = ink_of(colour_);
    // The icon chip in the Melodies colour, as the tabs' chips are in theirs.
    const float half = k.s(tok::kFace) * 0.5f;
    dl->AddRectFilled({s.icon.cx() - half, s.icon.cy() - half}, {s.icon.cx() + half, s.icon.cy() + half},
                      u32(with_alpha(own, tok::kSelectedFill)), k.s(tok::kRadiusControl));
    glyph(k, dl, path_icon(path.icon), {s.icon.cx(), s.icon.cy()}, k.s(tok::kIcon), own);
    // The name over its count, each a line of its own in the header's height.
    FitText count;
    count.format("%d of %d", progress.passed, progress.total);
    const FitText name = fit(k.bold, k.s(tok::kBody), path.title, s.title.w());
    const Box name_slot{s.title.x0, s.title.y0, s.title.x1, s.title.y0 + s.title.h() * melody_tok::kHeadNameShare};
    const Box count_slot{s.title.x0, s.title.y1 - s.title.h() * melody_tok::kHeadCountShare, s.title.x1, s.title.y1};
    label_at(k, dl, k.bold, k.s(tok::kBody), name_slot, LabelAlign::Left, col::kText, name.c_str());
    label_at(k, dl, k.regular, k.s(tok::kLabel), count_slot, LabelAlign::Left, col::kText2, count.c_str());
    // "Next · Oh! Susanna: small leaps".
    if (shows(s.next)) {
        const float size = k.s(melody_tok::kNextWords);
        if (std::abs(size - next_prefix_size_) > 0.01f) {
            next_prefix_w_ = label_width(k.regular, size, "Next \xC2\xB7");
            next_prefix_size_ = size;
        }
        if (progress.next < 0) {
            label_at(k, dl, k.regular, size, s.next, LabelAlign::Left, col::kText2, "All passed");
        } else {
            const MelodyPiece& piece = book.pieces[path.pieces[static_cast<std::size_t>(progress.next)]];
            const float gap = k.s(tok::kS1);
            const Box prefix{s.next.x0, s.next.y0, s.next.x0 + next_prefix_w_, s.next.y1};
            label_at(k, dl, k.regular, size, prefix, LabelAlign::Left, col::kText2,
                     progress.started ? "Next \xC2\xB7" : "First \xC2\xB7");
            // The goal reads on after the title, so its first letter is a small one.
            FitText words;
            const char small = static_cast<char>(std::tolower(static_cast<unsigned char>(piece.goal[0])));
            if (piece.tag.empty()) {
                words.format("%s: %c%s", piece.title.c_str(), small, piece.goal.c_str() + 1);
            } else {
                words.format("%s (%s): %c%s", piece.title.c_str(), piece.tag.c_str(), small, piece.goal.c_str() + 1);
            }
            const float room = s.next.x1 - (prefix.x1 + gap);
            const FitText cut = fit(k.regular, size, words.view(), room);
            const Box rest{prefix.x1 + gap, s.next.y0, s.next.x1, s.next.y1};
            label_at(k, dl, k.regular, size, rest, LabelAlign::Left, col::kText, cut.c_str());
        }
    }
    // Continue, Start or Again: the next piece (the first, once all are passed).
    char id[40];
    std::snprintf(id, sizeof id, "##melody-go-%zu", path_at);
    const bool pressed = tapped(press_area(k, id, s.button));
    const State st = item_state(k, false);
    const float face_h = k.s(tok::kFace);
    const Box face{s.button.x0, s.button.cy() - face_h * 0.5f, s.button.x1, s.button.cy() + face_h * 0.5f};
    dl->AddRectFilled({face.x0, face.y0}, {face.x1, face.y1}, u32(own), k.s(tok::kRadiusControl));
    draw_state(k, dl, id, face, s.button, st, Idle::None, k.s(tok::kRadiusControl));
    audit::note(id, {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
    const ImVec4 ink_on_own = state_ink(st, col::kAccentInk, true);
    if (s.button_words) {
        const std::size_t kind = button_kind(progress);
        const char* words = kButtonWords[kind];
        const float icon_size = k.s(tok::kIconSm);
        const float words_w = button_words_width(k, kind);
        const float gap = k.s(tok::kS2);
        const float x0 = face.cx() - (icon_size + gap + words_w) * 0.5f;
        glyph(k, dl, icon::kPlay, {x0 + icon_size * 0.5f, face.cy()}, icon_size, ink_on_own);
        audit::note("##melody-go-icon", {x0, face.y0}, {x0 + icon_size, face.y1}, InkKind::PlayTriangle);
        label_at(k, dl, k.bold, k.s(tok::kLabel), {x0 + icon_size + gap, face.y0, face.x1, face.y1},
                 LabelAlign::Left, ink_on_own, words);
    } else {
        glyph(k, dl, icon::kPlay, {face.cx(), face.cy()}, k.s(tok::kIcon), ink_on_own);
    }
    if (pressed) {
        const std::size_t place = static_cast<std::size_t>(std::max(progress.next, 0));
        asked->piece = static_cast<int>(path.pieces[place]);
    }
}

// The tiles of one row, as many as `count`, in the row's view: its strip scrolls them sideways, the
// next tile cut by the edge. A tile the edge cuts is shown but is not the audit's.
template <class One>
void MelodiesView::draw_row(Kit& k, const MelodiesLayout& lay, const MelodySectionBoxes& s,
                            std::size_t strip_at, std::size_t count, One&& one) {
    ChipStrip& strip = strips_[strip_at];
    strip.set_extent(s.content_w / k.u, s.row.w() / k.u);
    if (s.scrolls) {
        row_pointer(k, strip_at, clipped(s.row, lay.body));
    } else {
        strip.cancel();
    }
    const float offset = s.scrolls ? strip.offset() * k.u : 0.0f;
    const Box view = clipped(s.row, lay.body);
    ImGui::PushClipRect({view.x0, view.y0}, {view.x1, view.y1}, true);
    const bool was_quiet = audit::quiet();
    for (std::size_t place = 0; place < count; ++place) {
        const Box tile = melody_tile_at(s.row, static_cast<int>(place), lay.tile_w, lay.tile_h, k.u, offset);
        if (tile.x1 <= view.x0 || tile.x0 >= view.x1) {
            continue;
        }
        const Box visible = clipped(tile, view);
        const bool whole = visible.w() >= tile.w() - 0.5f && visible.h() >= tile.h() - 0.5f;
        whole_ += whole ? 1 : 0;
        audit::set_quiet(was_quiet || !whole);
        one(place, tile, visible);
    }
    audit::set_quiet(was_quiet);
    if (s.scrolls) {
        melody_draw_fades(k, view, strip.offset() > 0.5f / k.u,
                          strip.offset() < strip.max_offset() - 0.5f / k.u);
    }
    ImGui::PopClipRect();
}

void MelodiesView::draw_section(Kit& k, const MelodyBook& book,
                                std::size_t path_at, const MelodiesLayout& lay, MelodyAsk* asked) {
    const MelodyPath& path = book.paths[path_at];
    const MelodySectionBoxes& s = lay.section[path_at];
    if (s.whole.y1 < lay.body.y0 || s.whole.y0 > lay.body.y1) {
        strips_[path_at].cancel();
        return; // scrolled out of view
    }
    // The manifest's parser refuses a path over `kMelodyMaxPieces`, so every piece is counted.
    std::array<int, kMelodyMaxPieces> best{};
    const std::size_t count = path.pieces.size();
    for (std::size_t i = 0; i < count; ++i) {
        best[i] = book.pieces[path.pieces[i]].best_permille;
    }
    const MelodyPathProgress progress = melody_path_progress(std::span<const int>(best.data(), count));
    draw_head(k, book, path_at, progress, s, asked);
    draw_row(k, lay, s, path_at, count, [&](std::size_t place, const Box& tile, const Box& visible) {
        char id[40];
        std::snprintf(id, sizeof id, "##melody-%zu", path.pieces[place]);
        bool pressed = false;
        draw_tile(k, book.pieces[path.pieces[place]], titles_[path.pieces[place]], static_cast<int>(place),
                  static_cast<int>(place) == progress.next, tile, visible, lay, id, false, &pressed);
        if (pressed) {
            asked->piece = static_cast<int>(path.pieces[place]);
        }
    });
}

// "From your library": the header (the icon, the name over "6 of 212 songs", what the row is, and the
// way to the library) and the tiles under it, one for each song the sort chose.
void MelodiesView::draw_library(Kit& k, const LibraryTiles& library, const MelodiesLayout& lay,
                                MelodyAsk* asked) {
    const MelodySectionBoxes& s = lay.library;
    const std::size_t strip_at = kMelodyMaxPaths;
    if (s.whole.y1 < lay.body.y0 || s.whole.y0 > lay.body.y1) {
        strips_[strip_at].cancel();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 own = ink_of(colour_);
    const float half = k.s(tok::kFace) * 0.5f;
    dl->AddRectFilled({s.icon.cx() - half, s.icon.cy() - half}, {s.icon.cx() + half, s.icon.cy() + half},
                      u32(with_alpha(own, tok::kSelectedFill)), k.s(tok::kRadiusControl));
    glyph(k, dl, icon::kLibrary, {s.icon.cx(), s.icon.cy()}, k.s(tok::kIcon), own);
    FitText count;
    count.format("%zu fit a step", library.sorted);
    const FitText name = fit(k.bold, k.s(tok::kBody), "From your library", s.title.w());
    const Box name_slot{s.title.x0, s.title.y0, s.title.x1, s.title.y0 + s.title.h() * melody_tok::kHeadNameShare};
    const Box count_slot{s.title.x0, s.title.y1 - s.title.h() * melody_tok::kHeadCountShare, s.title.x1, s.title.y1};
    label_at(k, dl, k.bold, k.s(tok::kBody), name_slot, LabelAlign::Left, col::kText, name.c_str());
    label_at(k, dl, k.regular, k.s(tok::kLabel), count_slot, LabelAlign::Left, col::kText2, count.c_str());
    if (shows(s.next)) {
        const char* words = library.pieces.empty()
                                ? "No song fits a step yet. Add one from its details."
                                : "Sorted into the 5 steps by stars, hands and range";
        const FitText cut = fit(k.regular, k.s(melody_tok::kNextWords), words, s.next.w());
        label_at(k, dl, k.regular, k.s(melody_tok::kNextWords), s.next, LabelAlign::Left, col::kText2,
                 cut.c_str());
    }
    // Open library: the other way in, in the quiet button's look (the paths' Continue is the loud one).
    const char* id = "##melody-open-library";
    const bool pressed = tapped(press_area(k, id, s.button));
    const State st = item_state(k, false);
    const float face_h = k.s(tok::kFace);
    const Box face{s.button.x0, s.button.cy() - face_h * 0.5f, s.button.x1, s.button.cy() + face_h * 0.5f};
    dl->AddRectFilled({face.x0, face.y0}, {face.x1, face.y1}, u32(col::kFill), k.s(tok::kRadiusControl));
    draw_state(k, dl, id, face, s.button, st, Idle::None, k.s(tok::kRadiusControl));
    audit::note(id, {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
    const ImVec4 ink = state_ink(st, col::kText);
    if (s.button_words) {
        const float icon_size = k.s(tok::kIconSm);
        const float words_w = label_width(k.bold, k.s(tok::kLabel), "Open library");
        const float gap = k.s(tok::kS2);
        const float x0 = face.cx() - (icon_size + gap + words_w) * 0.5f;
        glyph(k, dl, icon::kLibrary, {x0 + icon_size * 0.5f, face.cy()}, icon_size, ink);
        label_at(k, dl, k.bold, k.s(tok::kLabel), {x0 + icon_size + gap, face.y0, face.x1, face.y1},
                 LabelAlign::Left, ink, "Open library");
    } else {
        glyph(k, dl, icon::kLibrary, {face.cx(), face.cy()}, k.s(tok::kIcon), ink);
    }
    asked->open_library = pressed;
    draw_row(k, lay, s, strip_at, library.pieces.size(),
             [&](std::size_t place, const Box& tile, const Box& visible) {
                 char tile_id[40];
                 std::snprintf(tile_id, sizeof tile_id, "##melody-library-%zu", place);
                 bool tile_pressed = false;
                 draw_tile(k, library.pieces[place], library_titles_[place], static_cast<int>(place), false,
                           tile, visible, lay, tile_id, true, &tile_pressed);
                 if (tile_pressed) {
                     asked->library = static_cast<int>(place);
                 }
             });
}

MelodyAsk MelodiesView::draw_body(Kit& k, const MelodyBook& book, const LibraryTiles& library,
                                  const MelodiesLayout& lay, RGBA colour) {
    MelodyAsk asked;
    whole_ = 0;
    colour_ = colour;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (book.paths.empty()) {
        label_at(k, dl, k.regular, k.s(tok::kBody), lay.body, LabelAlign::Centre, col::kText2,
                 "The bundled melodies could not be read.");
        return asked;
    }
    // The tiles' titles, laid out for the tile's title room (a library tile's is narrower where it has
    // no goal line to carry its mark).
    const float size = k.s(lay.goal ? tok::kBody : tok::kLabel);
    const MelodyTileBoxes shape =
        melody_tile_boxes({0.0f, 0.0f, lay.tile_w, lay.tile_h}, lay.goal, 3, k.u);
    if (titles_.size() != book.pieces.size() || std::abs(shape.title.w() - title_width_) >= 0.5f ||
        std::abs(size - title_size_) >= 0.01f) {
        wrap_titles(k, book.pieces, titles_, shape.title.w(), size, true);
        title_width_ = shape.title.w();
        title_size_ = size;
    }
    if (library.on) {
        const float library_w =
            melody_tile_boxes({0.0f, 0.0f, lay.tile_w, lay.tile_h}, lay.goal, 3, k.u, true).title.w();
        if (library_titles_for_ != library.serial || std::abs(library_w - library_title_width_) >= 0.5f ||
            std::abs(size - library_title_size_) >= 0.01f || library_two_lines_ != lay.goal) {
            wrap_titles(k, library.pieces, library_titles_, library_w, size, lay.goal);
            library_two_lines_ = lay.goal;
            library_titles_for_ = library.serial;
            library_title_width_ = library_w;
            library_title_size_ = size;
        }
    }
    ImGui::PushClipRect({lay.body.x0, lay.body.y0}, {lay.body.x1, lay.body.y1}, true);
    for (std::size_t i = 0; i < static_cast<std::size_t>(lay.sections) && i < book.paths.size(); ++i) {
        draw_section(k, book, i, lay, &asked);
    }
    if (library.on && lay.has_library) {
        draw_library(k, library, lay, &asked);
    }
    melody_draw_page_fades(k, lay.body, lay.scroll > 0.5f, lay.scroll < lay.max_scroll - 0.5f);
    ImGui::PopClipRect();
    return asked;
}

// The page: the header (back arrow, the page's name, the four tabs) and the body under it. A tap on a
// piece asks the port to open it in the player, and the screen closes once it does; a tap on a library
// tile opens that song through the library.
void LearnScreen::draw_melodies(Kit& k, UiModel& model, LearnPort& port, const Box& area) {
    AppSettings& app = model.app;
    const MelodyBook& book = port.melodies();
    const LibraryTiles& library = library_tiles(app, k.now);
    MelodyNeeds needs;
    needs.tabs = learn_measure_tabs(k, 3);
    needs.paths = static_cast<int>(book.paths.size()); // at most `kMelodyMaxPaths`: the parser refuses more
    for (int i = 0; i < needs.paths; ++i) {
        needs.pieces[static_cast<std::size_t>(i)] =
            static_cast<int>(book.paths[static_cast<std::size_t>(i)].pieces.size());
    }
    needs.library = library.on ? static_cast<int>(library.pieces.size()) : -1;
    const MelodiesLayout lay = melodies_.begin(k, area, needs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (draw_back(k, lay.back, "##learn-back")) {
        back();
        return;
    }
    label_at(k, dl, k.bold, k.s(tok::kTitle), lay.title, LabelAlign::Left, col::kText, "Melodies");
    dl->AddLine({area.x0, lay.header.y1}, {area.x1, lay.header.y1}, u32(col::kHairline),
                k.s(tok::kHairline));
    draw_tabs(k, model.look, lay.tabs, lay.tab_label, 3);
    const MelodyAsk asked =
        melodies_.draw_body(k, book, library, lay, learn_section_colour(model.look, LearnSection::Melodies));
    if (asked.open_library) {
        library_asked_ = true;
        return;
    }
    if (asked.library >= 0) {
        // A song of the player's library opens as any library song does, the screen closing as it is asked.
        library_->open(library_tiles_.index[static_cast<std::size_t>(asked.library)]);
        close();
        return;
    }
    if (asked.piece < 0) {
        return;
    }
    std::string why;
    if (port.play_melody(static_cast<std::size_t>(asked.piece), why)) {
        close();
    } else {
        k.say_free(why.empty() ? "That piece cannot be opened." : why, true);
    }
}

} // namespace etude::app::ui
