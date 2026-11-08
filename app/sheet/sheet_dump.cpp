// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_dump.hpp"

#include "sheet_engraver.hpp"
#include "sheet_job.hpp"
#include "sheet_name_fit.hpp"
#include "song_library.hpp"
#include "text.hpp"
#include "ui/tokens.hpp"

#include "etude/hands_present.hpp"
#include "etude/screen_layout.hpp"
#include "etude/sheet_map.hpp"
#include "etude/sheet_names.hpp"
#include "etude/sheet_view.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace etude::app {
namespace {

double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

// Blends `src` (straight alpha, tinted by `tint`) over the opaque RGBA `dst`.
void blend(std::vector<std::uint8_t>& dst, const std::vector<std::uint8_t>& src, int w, int x0,
           int y0, int x1, int y1, const std::uint8_t tint[3]) {
    for (int y = std::max(0, y0); y < y1; ++y) {
        for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
            const int a = src[i + 3];
            for (int c = 0; c < 3; ++c) {
                const int s = tint != nullptr ? tint[c] * src[i + static_cast<std::size_t>(c)] / 255 : src[i + static_cast<std::size_t>(c)];
                dst[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((s * a + dst[i + static_cast<std::size_t>(c)] * (255 - a)) / 255);
            }
        }
    }
}

} // namespace

int run_sheet_dump(const SheetDumpOptions& o) {
    const auto t0 = std::chrono::steady_clock::now();
    SongLoadOptions lo;
    lo.abc_chords = o.abc_chords;
    SongFileLoad load = load_song_file(o.song, lo);
    if (!load.ok) {
        std::printf("sheet-dump: %s\n", load.error.c_str());
        return 1;
    }
    load.song.absolute_path = o.song;
    const double load_ms = ms_since(t0);
    const auto t1 = std::chrono::steady_clock::now();
    const SheetSource source = make_sheet_source(load.song);
    const double source_ms = ms_since(t1);
    std::printf("sheet-dump: %s: %zu notes, loaded in %.1f ms; source %s in %.1f ms (%zu bytes)%s%s\n",
                load.song.title.c_str(), load.song.song.notes.size(), load_ms,
                source.status == SheetSourceStatus::Ok ? "ok" : "refused", source_ms,
                source.musicxml.size(), source.reason.empty() ? "" : ": ", source.reason.c_str());
    if (source.status != SheetSourceStatus::Ok) {
        return 2;
    }
    if (!o.out_dir.empty()) {
        std::filesystem::create_directories(o.out_dir);
        if (FILE* f = std::fopen((o.out_dir + "/source.musicxml").c_str(), "wb")) {
            std::fwrite(source.musicxml.data(), 1, source.musicxml.size(), f);
            std::fclose(f);
        }
    }
    const auto t2 = std::chrono::steady_clock::now();
    SheetEngraver engraver(sheet_resource_dir());
    const double init_ms = ms_since(t2);
    EngraveOptions eo;
    eo.width_px = o.width_px;
    eo.staff_space_px = o.staff_space_px;
    // Names: measured with the app's font (no window needed to measure).
    const bool names_on = !o.names.empty();
    LookSettings look;
    look.names_on_sheet = names_on;
    look.sheet_naming = o.names == "solfege" ? SheetNaming::Solfege : SheetNaming::Letters;
    look.sheet_chords = o.chord_symbols ? SheetChords::Symbols : SheetChords::NoteNames;
    Text font(nullptr);
    SheetNameTexts texts;
    if (names_on) {
        texts.make(look, font);
    }
    const bool two_staves = hands_present(load.song.song).both();
    const SheetNameStacks stacks = sheet_name_stacks(load.song.song, two_staves, o.chord_symbols);
    SheetPlan plan;
    if (o.window_w > 0 && o.window_h > 0) {
        const auto w = static_cast<float>(o.window_w);
        const auto h = static_cast<float>(o.window_h);
        const ScreenLayout lay = compute_layout(w, h, kDefaultPxPerMm, look);
        const float corner = ui::tok::kHit * ui_scale(w, h, kDefaultPxPerMm);
        plan = plan_sheet(score_area(lay), o.both ? ViewMode::Both : ViewMode::Sheet, true, two_staves,
                          kDefaultPxPerMm, corner,
                          names_on ? sheet_name_room(stacks, kDefaultPxPerMm, texts.line_em) : SheetNameRoom{});
        eo.width_px = std::round(plan.system_width_px);
        eo.staff_space_px = std::round(plan.staff_space_px * 4.0f) / 4.0f;
        std::printf("sheet-dump: %dx%d %s: %d system%s shown, staff space %.2f px, slot %.0f px, "
                    "system %.0f px wide; name stacks %d above, %d below\n",
                    o.window_w, o.window_h, o.both ? "Both" : "Sheet", plan.systems,
                    plan.systems == 1 ? "" : "s", static_cast<double>(plan.staff_space_px),
                    static_cast<double>(plan.slot_height_px), static_cast<double>(plan.system_width_px),
                    stacks.above, stacks.below);
    }
    const SheetNameSpacing spacing = sheet_name_spacing(names_on, look.sheet_naming);
    eo.spacing_linear = o.spacing_linear > 0.0f ? o.spacing_linear : spacing.linear;
    eo.spacing_non_linear = o.spacing_non_linear > 0.0f ? o.spacing_non_linear : spacing.non_linear;
    SheetLayout layout;
    std::string error;
    const auto t_done = std::chrono::steady_clock::now();
    if (!engraver.engrave(source.musicxml, eo, layout, error)) {
        std::printf("sheet-dump: engrave failed: %s\n", error.c_str());
        return 3;
    }
    const double engrave_ms = ms_since(t_done);
    const auto t4 = std::chrono::steady_clock::now();
    const SheetMap map = build_sheet_map(layout, load.song.song);
    const double map_ms = ms_since(t4);
    int notes = 0;
    int rests = 0;
    int stems = 0;
    int tied = 0;
    int tied_shown = 0;
    for (std::size_t i = 0; i < layout.marks.size(); ++i) {
        const SheetMark& m = layout.marks[i];
        (m.kind == SheetMarkKind::Note ? notes : m.kind == SheetMarkKind::Rest ? rests : stems) += 1;
        if (m.tied) {
            ++tied;
            tied_shown += map.mark_first[i + 1] > map.mark_first[i] ? 1 : 0;
        }
    }
    std::printf("sheet-dump: fonts %.1f ms, engrave %.1f ms: %zu systems, %zu bars, %d note marks "
                "(%d tied, %d of them showing a note), %d rests, %d chord stems; map %.1f ms: %d of "
                "%d practised notes matched (%s), path %zu points\n",
                init_ms, engrave_ms, layout.systems.size(), layout.bars.size(), notes, tied,
                tied_shown, rests, stems, map_ms, map.matched, map.practised,
                map.usable() ? "usable" : "NOT usable", map.path.size());
    if (o.verbose) {
        const Song& song = load.song.song;
        int shown = 0;
        for (std::uint32_t i = 0; i < song.notes.size() && shown < 12; ++i) {
            const Note& n = song.notes[i];
            if (n.drum || n.accompaniment || map.note_first[i] != map.note_first[i + 1]) {
                continue;
            }
            const std::size_t k = static_cast<std::size_t>(n.measure - song.measures.front().number);
            std::printf("  unmatched note %u: written bar %d, offset %.3f q, pitch %d\n", i,
                        n.written_measure(),
                        static_cast<double>(n.start_tick - song.measures[k].start_tick) /
                            song.tempo.ticks_per_quarter,
                        n.pitch);
            ++shown;
        }
    }
    if (names_on) {
        // The names as the view places them, every system, at the engraving's scale on screen.
        std::vector<float> unit(layout.marks.size(), 0.0f);
        for (std::size_t m = 0; m < layout.marks.size(); ++m) {
            const SheetMark& mk = layout.marks[m];
            if (mk.kind == SheetMarkKind::Note && mk.pitch >= 0 && mk.pitch < 128) {
                unit[m] = texts.unit_width[static_cast<std::size_t>(mk.pitch)];
            }
        }
        const float margin = plan.staff_space_px * kSheetMarginSpaces;
        float font_scale = 0.0f; // the widest system's, for the line below
        int shrunk = 0;          // systems drawn smaller to fit their slot
        SheetNames total;
        int placed = 0;
        SheetNames names;
        SheetSymbols symbols;
        std::vector<SheetNameColumn> columns;
        const bool flats = look.names_spelling == NameSpelling::Flats;
        const auto t_names = std::chrono::steady_clock::now();
        for (std::size_t s = 0; s < layout.systems.size(); ++s) {
            const SheetSystem& sys = layout.systems[s];
            const float scale = plan.system_width_px > 0.0f
                                    ? sheet_system_scale(sys, eo.width_px, plan.system_width_px,
                                                         plan.slot_height_px, margin)
                                    : 1.0f;
            font_scale = std::max(font_scale, scale);
            shrunk += plan.system_width_px > 0.0f && scale < plan.system_width_px / std::max(1.0f, eo.width_px) - 1e-4f;
            SheetNameStyle st = sheet_name_style(eo.staff_space_px, scale, kDefaultPxPerMm, texts);
            const float half = plan.slot_height_px * 0.5f / scale;
            const float mid = (sys.staff_top + sys.staff_bottom) * 0.5f;
            st.top_limit = half > 0.0f ? mid - half : -1e9f;
            st.bottom_limit = half > 0.0f ? mid + half : 1e9f;
            sheet_name_columns(layout, static_cast<int>(s), unit, o.chord_symbols, columns);
            symbols.name_columns(columns, flats, texts.line_em, font);
            place_sheet_names(layout, static_cast<int>(s), columns, unit, st, names);
            for (const SheetName& n : names.names) {
                placed += n.kind == SheetNameKind::More ? 0 : 1;
            }
            total.staggered += names.staggered;
            total.sloped += names.sloped;
            total.smaller += names.smaller;
            total.symbols += names.symbols;
            total.left_out += names.left_out;
            total.marks += names.marks;
            total.dropped += names.dropped;
            if (o.verbose && (names.sloped > 0 || names.marks > 0 || names.dropped > 0)) {
                std::printf("  system %zu: %d sloped, %d smaller, %d left out under %d marks, %d dropped\n",
                            s + 1, names.sloped, names.smaller, names.left_out, names.marks, names.dropped);
            }
        }
        std::printf("sheet-dump: names (%s%s, %.0f px font, spacing %.2f/%.2f, placed in %.1f ms): %d "
                    "placed (%d chord symbols), %d staggered, %d sloped, %d smaller, %d left out "
                    "under %d marks, %d dropped over %zu systems (%d drawn smaller to fit their slot)\n",
                    o.names.c_str(), o.chord_symbols ? " + chord symbols" : "",
                    static_cast<double>(sheet_name_font_px(eo.staff_space_px * font_scale, kDefaultPxPerMm)),
                    static_cast<double>(eo.spacing_linear), static_cast<double>(eo.spacing_non_linear),
                    ms_since(t_names), placed, total.symbols, total.staggered, total.sloped,
                    total.smaller, total.left_out, total.marks, total.dropped, layout.systems.size(), shrunk);
    }
    double raster_total = 0.0;
    double raster_first = 0.0;
    for (std::size_t s = 0; s < layout.systems.size(); ++s) {
        SystemPicture pic;
        const auto t5 = std::chrono::steady_clock::now();
        if (!engraver.draw_system(static_cast<int>(s), pic)) {
            std::printf("sheet-dump: system %zu did not draw\n", s);
            continue;
        }
        const double ms = ms_since(t5);
        raster_total += ms;
        if (s == 0) {
            raster_first = ms;
        }
        if (o.out_dir.empty() || s >= static_cast<std::size_t>(o.max_pictures)) {
            continue;
        }
        // Paper, the page, the notes in ink with the first half of the song green, the cursor at
        // the song's middle.
        std::vector<std::uint8_t> rgba(pic.base.size());
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i] = 0xFF;
            rgba[i + 1] = 0xFE;
            rgba[i + 2] = 0xF8;
            rgba[i + 3] = 0xFF;
        }
        blend(rgba, pic.base, pic.width, 0, 0, pic.width, pic.height, nullptr);
        const double mid = static_cast<double>(load.song.song.end_tick) * 0.5;
        for (std::uint32_t m = 0; m < layout.marks.size(); ++m) {
            const SheetMark& mk = layout.marks[m];
            if (mk.system != static_cast<int>(s) || mk.kind == SheetMarkKind::Rest) {
                continue;
            }
            const int note = sheet_mark_note(map, load.song.song, m, mid);
            static constexpr std::uint8_t kInk[3] = {26, 26, 31};
            static constexpr std::uint8_t kGreen[3] = {26, 158, 64};
            const bool played = note >= 0;
            blend(rgba, pic.notes, pic.width, static_cast<int>(mk.box.x) - 1,
                  static_cast<int>(mk.box.y) - 1, static_cast<int>(mk.box.x + mk.box.w) + 2,
                  static_cast<int>(mk.box.y + mk.box.h) + 2, played ? kGreen : kInk);
        }
        const SheetCursor c = sheet_cursor_at(map, mid);
        if (c.system == static_cast<int>(s)) {
            const SheetSystem& sys = layout.systems[s];
            for (int y = static_cast<int>(sys.staff_top); y < static_cast<int>(sys.staff_bottom); ++y) {
                for (int x = static_cast<int>(c.x) - 2; x <= static_cast<int>(c.x) + 2; ++x) {
                    if (x >= 0 && x < pic.width && y >= 0 && y < pic.height) {
                        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(pic.width) + static_cast<std::size_t>(x)) * 4;
                        rgba[i] = 42;
                        rgba[i + 1] = 111;
                        rgba[i + 2] = 240;
                    }
                }
            }
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(pic.width, pic.height, SDL_PIXELFORMAT_RGBA32,
                                                     rgba.data(), pic.width * 4);
        char name[64];
        std::snprintf(name, sizeof name, "/system-%02zu.png", s + 1);
        if (surface != nullptr) {
            SDL_SavePNG(surface, (o.out_dir + name).c_str());
            SDL_DestroySurface(surface);
        }
    }
    std::printf("sheet-dump: drew %zu systems in %.1f ms (first %.1f ms); first picture after "
                "%.1f ms from the song's load\n",
                layout.systems.size(), raster_total, raster_first,
                source_ms + init_ms + engrave_ms + map_ms + raster_first);
    return map.usable() ? 0 : 4;
}

} // namespace etude::app
