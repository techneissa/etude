// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_view.hpp"

#include "sheet_view_internal.hpp"

#include "../gfx.hpp"
#include "../touch_device.hpp"
#include "sheet_name_fit.hpp"

#include "etude/hands_present.hpp"
#include "etude/note_colours.hpp"
#include "etude/screen_layout.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace etude::app {

namespace {

// Note name text slots kept (one per name and size) before the cache starts again.
constexpr std::size_t kMaxNameSlots = 512;
// A note head's colour ring (the engraver's `rings` picture, sheet_ring_px wide): the hand not
// practised is drawn fainter, as its notes are; once the note is graded the ring fades out in
// `kRingFadeS`, so only the grade's green or amber is left (at once with reduced effects).
constexpr float kOtherHandAlpha = 0.38f;

} // namespace

void SheetView::set_helps(const LookSettings& look, float px_per_mm) {
    // Only notes what changed: the names and colours are made when a score shows that needs them
    // (ready_helps), so a look changed on the settings pages costs nothing here.
    if (helps_primed_ && look == helps_look_ && px_per_mm == px_per_mm_) {
        return;
    }
    naming_dirty_ = naming_dirty_ || !helps_primed_ || look.sheet_naming != helps_look_.sheet_naming ||
                    look.sheet_chords != helps_look_.sheet_chords ||
                    look.names_show_octave != helps_look_.names_show_octave ||
                    look.names_spelling != helps_look_.names_spelling ||
                    look.octave_numbering != helps_look_.octave_numbering;
    colours_dirty_ = true;
    if (!helps_primed_ || px_per_mm != px_per_mm_ || look.names_on_sheet != helps_look_.names_on_sheet) {
        ++names_serial_;
    }
    helps_look_ = look;
    px_per_mm_ = px_per_mm;
    names_on_ = look.names_on_sheet;
    colours_on_ = look.sheet_note_colours;
    helps_primed_ = true;
}

void SheetView::ready_helps() {
    if (names_on_ && naming_dirty_) {
        names_.make(helps_look_, text_); // letters or Do Re Mi by the sheet's own choice
        name_slots_.clear();
        symbols_.clear();
        naming_dirty_ = false;
        ++names_serial_;
    }
    if (colours_on_ && colours_dirty_) {
        // The colours Colour by Note gives the blocks, High contrast included.
        LookSettings by_note = helps_look_;
        by_note.color_scheme = ColorScheme::ByPitchClass;
        const NoteColourTable table(by_note);
        for (int p = 0; p < 128; ++p) {
            const RGBA c = table.colour(p, 100, Hand::Right);
            note_colour_[static_cast<std::size_t>(p)] = {static_cast<float>(c.r) / 255.0f,
                                                        static_cast<float>(c.g) / 255.0f,
                                                        static_cast<float>(c.b) / 255.0f, 1.0f};
        }
        colours_dirty_ = false;
    }
}

void SheetView::place_names(Engraving& e) {
    e.names_serial = names_serial_;
    e.names.assign(e.layout.systems.size(), SheetNames{});
    // The totals are this engraving's, none with names off.
    if (!names_on_) {
        return;
    }
    unit_width_of_mark_.assign(e.layout.marks.size(), 0.0f);
    for (std::size_t m = 0; m < e.layout.marks.size(); ++m) {
        const SheetMark& mk = e.layout.marks[m];
        if (mk.kind == SheetMarkKind::Note && mk.pitch >= 0 && mk.pitch < 128) {
            unit_width_of_mark_[m] = names_.unit_width[static_cast<std::size_t>(mk.pitch)];
        }
    }
    const bool chords = helps_look_.sheet_chords == SheetChords::Symbols;
    const bool flats = helps_look_.names_spelling == NameSpelling::Flats; // settled by the key
    if (name_slots_.size() > kMaxNameSlots) {
        name_slots_.clear(); // made again below for this engraving's names
    }
    for (std::size_t s = 0; s < e.layout.systems.size(); ++s) {
        const SheetSystem& sys = e.layout.systems[s];
        // The names' size on the screen, in the picture drawn at the system's scale (draw()); the
        // slot round the system, in its picture: the staves sit in the middle of it.
        const float scale = name_width_px_ > 0.0f
                                ? sheet_system_scale(sys, e.options.width_px, name_width_px_, name_slot_px_, name_margin_px_)
                                : 1.0f;
        SheetNameStyle st = sheet_name_style(e.options.staff_space_px, scale, px_per_mm_, names_);
        const float half = name_slot_px_ * 0.5f / std::max(0.01f, scale);
        const float mid = (sys.staff_top + sys.staff_bottom) * 0.5f;
        st.top_limit = half > 0.0f ? mid - half : -1e9f;
        st.bottom_limit = half > 0.0f ? mid + half : 1e9f;
        sheet_name_columns(e.layout, static_cast<int>(s), unit_width_of_mark_, chords, name_columns_);
        symbols_.name_columns(name_columns_, flats, names_.line_em, text_);
        SheetNames& out = e.names[s];
        place_sheet_names(e.layout, static_cast<int>(s), name_columns_, unit_width_of_mark_, st, out);
        // Every name's text made now, at the size it draws at, so drawing never makes one.
        for (const SheetName& n : out.names) {
            name_slot(e, n, std::round(n.size * scale / names_.line_em));
        }
    }
}

void SheetView::draw_rings(const Engraving& e, const Picture& pic, int system, float x, float y,
                           float scale) {
    if (pic.rings == nullptr) {
        return;
    }
    const auto& marks = e.marks_of_system[static_cast<std::size_t>(system)];
    const float tw = static_cast<float>(pic.width);
    const float th = static_cast<float>(pic.height);
    const float r = sheet_ring_px(e.options.staff_space_px) + 1.0f; // the ring and its soft edge
    std::size_t q = 0;
    for (const std::uint32_t m : marks) {
        const SheetMark& mk = e.layout.marks[m];
        if (mk.kind != SheetMarkKind::Note || mk.pitch < 0 || mk.pitch > 127 || mk.head.empty() ||
            q * 4 + 4 > vertices_.size() || ring_alpha_[m] <= 0.0f) {
            continue;
        }
        const float x0 = std::max(0.0f, mk.head.x - r);
        const float y0 = std::max(0.0f, mk.head.y - r);
        const float x1 = std::min(tw, mk.head.right() + r);
        const float y1 = std::min(th, mk.head.bottom() + r);
        SDL_FColor c = note_colour_[static_cast<std::size_t>(mk.pitch)];
        c.a = ring_alpha_[m] * (mark_colour_[m] == kOtherHand ? kOtherHandAlpha : 1.0f);
        SDL_Vertex* v = &vertices_[q * 4];
        v[0] = {{x + x0 * scale, y + y0 * scale}, c, {x0 / tw, y0 / th}};
        v[1] = {{x + x1 * scale, y + y0 * scale}, c, {x1 / tw, y0 / th}};
        v[2] = {{x + x1 * scale, y + y1 * scale}, c, {x1 / tw, y1 / th}};
        v[3] = {{x + x0 * scale, y + y1 * scale}, c, {x0 / tw, y1 / th}};
        ++q;
    }
    if (q > 0) {
        gfx::geometry(renderer_, pic.rings, vertices_.data(), static_cast<int>(q * 4),
                      indices_.data(), static_cast<int>(q * 6));
    }
}

TextSlot* SheetView::name_slot(const Engraving& e, const SheetName& n, float px) {
    std::uint32_t id = 0;
    const std::string* text = nullptr;
    if (n.kind == SheetNameKind::Note && n.mark < e.layout.marks.size()) {
        id = static_cast<std::uint32_t>(e.layout.marks[n.mark].pitch);
        text = &names_.text[id];
    } else if (n.kind == SheetNameKind::Symbol && n.symbol >= 0 &&
               static_cast<std::size_t>(n.symbol) < symbols_.text.size()) {
        id = static_cast<std::uint32_t>(n.symbol);
        text = &symbols_.text[id];
    }
    if (text == nullptr || px < 1.0f) {
        return nullptr;
    }
    const int outline = std::max(1, static_cast<int>(std::lround(px * 0.08f)));
    const std::uint32_t key = std::min<std::uint32_t>(static_cast<std::uint32_t>(px), 1023u) | (id << 10) |
                              (static_cast<std::uint32_t>(n.kind) << 30);
    TextSlot& slot = name_slots_[key];
    if (!Text::is_prepared(slot, *text, px, outline)) {
        text_.prepare_exact(slot, *text, px);
        text_.prepare_outline(slot, outline);
    }
    return &slot;
}

void SheetView::draw_names(const Engraving& e, int system, float x, float y, float scale) {
    if (static_cast<std::size_t>(system) >= e.names.size()) {
        return;
    }
    const SheetNames& names = e.names[static_cast<std::size_t>(system)];
    if (names.names.empty()) {
        return;
    }
    for (const SheetName& n : names.names) {
        const float px = std::round(n.size * scale / names_.line_em);
        if (px < 1.0f) {
            continue;
        }
        SDL_FColor ink = kMarkColours[kInk];
        if (mark_colour_[n.mark] == kOtherHand) {
            ink.a = kOtherHandAlpha;
        }
        if (n.kind == SheetNameKind::More) {
            // Three quiet dots where names were left out.
            const float dot = std::max(kSheetNameMoreDotMinPx, std::round(n.box.h * scale));
            const float cx = x + (n.box.x + n.box.w * 0.5f) * scale;
            const float top = std::round(y + (n.box.y + n.box.h * 0.5f) * scale - dot * 0.5f);
            const std::array<SDL_FRect, 3> dots{{{std::round(cx - 2.5f * dot), top, dot, dot},
                                                 {std::round(cx - 0.5f * dot), top, dot, dot},
                                                 {std::round(cx + 1.5f * dot), top, dot, dot}}};
            gfx::set_blend(renderer_, SDL_BLENDMODE_BLEND);
            gfx::set_colour(renderer_, ink.r, ink.g, ink.b, ink.a * kSheetNameMoreAlpha);
            gfx::fill_rects(renderer_, dots.data(), static_cast<int>(dots.size()));
            continue;
        }
        TextSlot* found = name_slot(e, n, px);
        if (found == nullptr) {
            continue;
        }
        TextSlot& slot = *found;
        const float top = y + n.box.y * scale + (n.box.h * scale - slot.height) * 0.5f;
        text_.draw_outlined(slot, std::round(x + (n.box.x + n.box.w * 0.5f) * scale), std::round(top),
                            Align::Centre, ink, kPaper);
    }
    if (system == cursor_.system) {
    }
}

SheetView::~SheetView() {
    drop_pictures(front_);
    drop_pictures(back_);
}

} // namespace etude::app
