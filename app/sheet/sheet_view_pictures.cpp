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

// Systems with pictures kept per engraving.
constexpr std::size_t kMaxPictures = 6;
// Pictures that fail in a row (not drawn, or no texture) before the song's sheet music is given up.
constexpr int kMaxFailedPictures = 3;

} // namespace

void SheetView::drop_pictures(Engraving& e) {
    for (Picture& p : e.pictures) {
        if (p.base != nullptr) {
            gfx::destroy_texture(p.base);
        }
        if (p.notes != nullptr) {
            gfx::destroy_texture(p.notes);
        }
        if (p.rings != nullptr) {
            gfx::destroy_texture(p.rings);
        }
    }
    e.pictures.clear();
}

void SheetView::drop_renderer() {
    drop_pictures(front_);
    drop_pictures(back_);
    wanted_.fill(-2); // ask for the pictures again
}

void SheetView::adopt_renderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
    text_.set_renderer(renderer);
}

const SheetView::Picture* SheetView::picture_of(const Engraving& e, int system) const {
    for (const Picture& p : e.pictures) {
        if (p.system == system) {
            return &p;
        }
    }
    return nullptr;
}

SheetOffer SheetView::offer() const {
    SheetOffer o;
    o.song = has_song_;
    o.known = answered_;
    o.offered = answered_ && offered_;
    if (answered_ && !offered_) {
        o.reason = reason_;
    }
    return o;
}

void SheetView::sync_song(const SongController& songs, std::int64_t now_ns) {
    (void)now_ns;
    if (songs.notes_serial() == song_serial_ && songs.has_song() == has_song_) {
        return;
    }
    song_serial_ = songs.notes_serial();
    has_song_ = songs.has_song();
    drop_pictures(front_);
    drop_pictures(back_);
    front_ = Engraving{};
    back_ = Engraving{};
    asked_ = 0;
    asked_options_ = {};
    want_options_ = {};
    answered_ = false;
    offered_ = false;
    reason_.clear();
    failed_pictures_ = 0;
    song_.reset();
    song_job_ns_ = 0;
    two_staves_ = false;
    stats_ = SheetViewStats{};
    turn_from_ = turn_to_ = top_ = 0.0f;
    drop_gesture();
    request_count_ = 0;
    loop_first_ = loop_last_ = -1;
    if (!has_song_) {
        return;
    }
    const Song& song = songs.song().song;
    int count = 0;
    for (const Note& n : song.notes) {
        count += n.drum || n.accompaniment ? 0 : 1;
    }
    if (count > kMaxSheetNotes) {
        answered_ = true;
        reason_ = "the song is too long for sheet music";
        return;
    }
    two_staves_ = hands_present(song).both();
    stacks_ = sheet_name_stacks(song, two_staves_);
    chord_stacks_ = sheet_name_stacks(song, two_staves_, true);
    song_ = std::make_shared<const LoadedSong>(songs.song());
}

void SheetView::take_results(std::int64_t now_ns) {
    SheetLayoutReady layout;
    while (worker_->take_layout(layout)) {
        if (layout.generation != asked_) {
            continue; // stale: a newer job was sent
        }
        answered_ = true;
        if (!layout.ok) {
            if (!front_.ready) {
                offered_ = false;
                reason_ = layout.reason;
            }
            continue;
        }
        offered_ = true;
        drop_pictures(back_);
        back_ = Engraving{};
        back_.generation = layout.generation;
        back_.ready = true;
        back_.options = layout.options;
        back_.layout = std::move(layout.layout);
        back_.map = std::move(layout.map);
        back_.marks_of_system.assign(back_.layout.systems.size(), {});
        int written_bars = 0;
        for (const SheetBar& b : back_.layout.bars) {
            written_bars = std::max(written_bars, b.bar + 1);
        }
        back_.box_of_written.assign(static_cast<std::size_t>(written_bars), -1);
        for (std::size_t i = 0; i < back_.layout.bars.size(); ++i) {
            const int w = back_.layout.bars[i].bar;
            if (w >= 0 && back_.box_of_written[static_cast<std::size_t>(w)] < 0) {
                back_.box_of_written[static_cast<std::size_t>(w)] = static_cast<int>(i);
            }
        }
        back_.pictures.reserve(kMaxPictures + 2);
        for (std::uint32_t m = 0; m < back_.layout.marks.size(); ++m) {
            const SheetMark& mk = back_.layout.marks[m];
            if (mk.kind != SheetMarkKind::Rest && mk.system >= 0 &&
                static_cast<std::size_t>(mk.system) < back_.marks_of_system.size()) {
                back_.marks_of_system[static_cast<std::size_t>(mk.system)].push_back(m);
            }
        }
    }
    // One picture a frame: two textures made and filled.
    SheetPictureReady pic;
    if (!worker_->take_picture(pic)) {
        return;
    }
    Engraving* e = nullptr;
    if (back_.ready && pic.generation == back_.generation) {
        e = &back_;
    } else if (front_.ready && pic.generation == front_.generation) {
        e = &front_;
    }
    if (e == nullptr || picture_of(*e, pic.system) != nullptr) {
        return;
    }
    if (pic.picture.width <= 0 || pic.picture.height <= 0) {
        picture_failed();
        return;
    }
    Picture p;
    p.system = pic.system;
    p.width = pic.picture.width;
    p.height = pic.picture.height;
    p.base = gfx::create_texture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                 p.width, p.height);
    p.notes = gfx::create_texture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                  p.width, p.height);
    p.rings = gfx::create_texture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                  p.width, p.height);
    if (p.base == nullptr || p.notes == nullptr || p.rings == nullptr) {
        for (SDL_Texture* t : {p.base, p.notes, p.rings}) {
            if (t != nullptr) {
                gfx::destroy_texture(t);
            }
        }
        picture_failed();
        return;
    }
    failed_pictures_ = 0;
    gfx::update_texture(p.base, pic.picture.base.data(), p.width * 4);
    gfx::update_texture(p.notes, pic.picture.notes.data(), p.width * 4);
    if (pic.picture.rings.size() == pic.picture.notes.size()) {
        gfx::update_texture(p.rings, pic.picture.rings.data(), p.width * 4);
    }
    for (SDL_Texture* t : {p.base, p.notes, p.rings}) {
        gfx::set_texture_blend(t, SDL_BLENDMODE_BLEND);
        gfx::set_texture_linear(t);
    }
    e->pictures.push_back(p);
    if (stats_.first_picture_ms <= 0.0 && song_job_ns_ > 0) {
        stats_.first_picture_ms = static_cast<double>(now_ns - song_job_ns_) / 1e6;
    }
}

void SheetView::picture_failed() {
    // Ask again (the worker draws a system again only when asked); after a few failures in a row
    // the song has no sheet music, and the switch says why.
    wanted_.fill(-2);
    if (++failed_pictures_ < kMaxFailedPictures) {
        return;
    }
    drop_pictures(front_);
    drop_pictures(back_);
    front_ = Engraving{};
    back_ = Engraving{};
    offered_ = false;
    reason_ = "the score could not be drawn";
}

void SheetView::ask_pictures(Engraving& e, int cursor_system) {
    const int count = static_cast<int>(e.layout.systems.size());
    const int c = std::clamp(cursor_system, 0, std::max(0, count - 1));
    std::array<int, 4> wanted{};
    std::size_t n = 0;
    for (const int s : {c, c + 1, c + 2, c - 1}) {
        if (s >= 0 && s < count) {
            wanted[n++] = s;
        }
    }
    // Pictures far from the cursor go first when there are too many.
    while (e.pictures.size() > kMaxPictures) {
        auto far = std::max_element(e.pictures.begin(), e.pictures.end(),
                                    [&](const Picture& a, const Picture& b) {
                                        return std::abs(a.system - c) < std::abs(b.system - c);
                                    });
        if (far->base != nullptr) {
            gfx::destroy_texture(far->base);
        }
        if (far->notes != nullptr) {
            gfx::destroy_texture(far->notes);
        }
        if (far->rings != nullptr) {
            gfx::destroy_texture(far->rings);
        }
        e.pictures.erase(far);
    }
    // Ask only for what is missing, and only when that changes.
    std::array<int, 8> missing{};
    missing.fill(-1);
    std::size_t m = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (picture_of(e, wanted[i]) == nullptr) {
            missing[m++] = wanted[i];
        }
    }
    missing[7] = static_cast<int>(e.generation & 0x7fffffff);
    if (missing == wanted_) {
        return;
    }
    wanted_ = missing;
    worker_->want_pictures(e.generation, std::span<const int>(missing.data(), m));
}

} // namespace etude::app
