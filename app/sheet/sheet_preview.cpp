// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_preview.hpp"

#include "../frame_log.hpp"
#include "../gfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace etude::app {

namespace {

constexpr ImU32 kPaper = IM_COL32(255, 254, 248, 255);
constexpr ImU32 kInk = IM_COL32(0, 0, 0, 255);
constexpr ImU32 kCursor = IM_COL32(42, 111, 240, 200);
// Pictures that come back empty this many times in a row: no sheet music.
constexpr int kMaxFailures = 3;
// A size within this many pixels of the one engraved is not engraved again.
constexpr float kSameWidthPx = 8.0f;
// A new size is engraved once it has held still this long.
constexpr std::chrono::milliseconds kResizeStill{150};
// The systems asked for at a time: the cursor's and the next ones.
constexpr int kAhead = 3;
// Between two systems on the paper, and the cursor's width and reach, in staff spaces.
constexpr float kGapSpaces = 1.5f;
constexpr float kCursorSpaces = 0.5f;
constexpr float kCursorReachSpaces = 1.0f;

ImTextureRef texture_ref(SDL_Texture* t) {
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(t)));
}

} // namespace

SheetPreview::SheetPreview(SDL_Renderer* renderer) : renderer_(renderer) {
    wanted_.reserve(kAhead);
}

SheetPreview::~SheetPreview() { drop_pictures(); }

void SheetPreview::drop_pictures() {
    for (Picture& p : pictures_) {
        for (SDL_Texture* t : {p.base, p.notes}) {
            if (t != nullptr) {
                gfx::destroy_texture(t);
            }
        }
    }
    pictures_.clear();
}

void SheetPreview::clear() {
    drop_pictures();
    song_.reset();
    status_ = Status::None;
    reason_.clear();
    layout_ = {};
    map_ = {};
    ++generation_; // whatever the worker still sends is stale
}

void SheetPreview::want(const std::shared_ptr<const LoadedSong>& song, float width_px,
                        float staff_space_px) {
    if (!song) {
        if (song_) {
            clear();
        }
        return;
    }
    if (song == song_ && std::abs(width_px - options_.width_px) < kSameWidthPx) {
        resize_px_ = -1.0f;
        return;
    }
    if (song == song_) {
        // The pictures at the old size stay up meanwhile.
        const auto now = std::chrono::steady_clock::now();
        if (std::abs(width_px - resize_px_) >= 1.0f) {
            resize_px_ = width_px;
            resize_since_ = now;
            return;
        }
        if (now - resize_since_ < kResizeStill) {
            return;
        }
    }
    resize_px_ = -1.0f;
    drop_pictures();
    ++generation_; // whatever the worker still sends for the last song or size is stale
    const bool same_song = song == song_;
    song_ = song;
    options_ = {};
    options_.width_px = width_px;
    options_.staff_space_px = staff_space_px;
    if (!same_song) {
        ++serial_;
        first_note_s_ = 0.0;
        bool any = false;
        for (const Note& n : song->song.notes) {
            if (n.drum) {
                continue;
            }
            first_note_s_ = any ? std::min(first_note_s_, n.start_s) : n.start_s;
            any = true;
        }
    }
    int count = 0;
    for (const Note& n : song->song.notes) {
        count += n.drum || n.accompaniment ? 0 : 1;
    }
    layout_ = {};
    map_ = {};
    top_ = 0;
    failures_ = 0;
    reason_.clear();
    if (count > kMaxSheetNotes) {
        status_ = Status::NoSheet;
        reason_ = "the song is too long for sheet music";
        return;
    }
    status_ = Status::Waiting;
    if (!worker_) {
        worker_ = std::make_unique<SheetWorker>();
    }
    SheetJob job;
    job.generation = ++generation_;
    job.song_serial = serial_;
    job.song = song;
    job.options = options_;
    worker_->engrave(std::move(job));
}

const SheetPreview::Picture* SheetPreview::picture_of(int system) const {
    for (const Picture& p : pictures_) {
        if (p.system == system) {
            return &p;
        }
    }
    return nullptr;
}

void SheetPreview::update() {
    if (!worker_) {
        return;
    }
    SheetLayoutReady layout;
    while (worker_->take_layout(layout)) {
        if (layout.generation != generation_) {
            continue;
        }
        if (!layout.ok) {
            status_ = Status::NoSheet;
            reason_ = layout.reason;
            continue;
        }
        layout_ = std::move(layout.layout);
        map_ = std::move(layout.map);
        status_ = Status::Ready;
        pictures_.reserve(layout_.systems.size());
    }
    // One picture a frame.
    SheetPictureReady pic;
    if (worker_->take_picture(pic) && pic.generation == generation_ && status_ == Status::Ready &&
        picture_of(pic.system) == nullptr) {
        if (pic.picture.width <= 0 || pic.picture.height <= 0) {
            if (++failures_ >= kMaxFailures) {
                status_ = Status::NoSheet;
                reason_ = "the score could not be drawn";
            }
        } else {
            const FrameDetailScope timing(FrameDetail::Texture);
            Picture p;
            p.system = pic.system;
            p.width = pic.picture.width;
            p.height = pic.picture.height;
            p.base = gfx::create_texture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, p.width, p.height);
            p.notes = gfx::create_texture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STATIC, p.width, p.height);
            if (p.base != nullptr && p.notes != nullptr) {
                failures_ = 0;
                gfx::update_texture(p.base, pic.picture.base.data(), p.width * 4);
                gfx::update_texture(p.notes, pic.picture.notes.data(), p.width * 4);
                for (SDL_Texture* t : {p.base, p.notes}) {
                    gfx::set_texture_blend(t, SDL_BLENDMODE_BLEND);
                    gfx::set_texture_linear(t);
                }
                pictures_.push_back(p);
            } else {
                for (SDL_Texture* t : {p.base, p.notes}) {
                    if (t != nullptr) {
                        gfx::destroy_texture(t);
                    }
                }
                ++failures_;
            }
        }
    }
    if (status_ != Status::Ready) {
        return;
    }
    // Pictures well behind the cursor go (a long song would keep hundreds); a loop back to the
    // start asks for its first ones again.
    for (std::size_t i = 0; i < pictures_.size();) {
        if (pictures_[i].system < top_ - 1 || pictures_[i].system > top_ + 2 * kAhead) {
            for (SDL_Texture* t : {pictures_[i].base, pictures_[i].notes}) {
                if (t != nullptr) {
                    gfx::destroy_texture(t);
                }
            }
            pictures_[i] = pictures_.back();
            pictures_.pop_back();
        } else {
            ++i;
        }
    }
    // The cursor's system and the next ones, those not made yet.
    wanted_.clear();
    const int systems = static_cast<int>(layout_.systems.size());
    for (int s = top_; s < std::min(systems, top_ + kAhead); ++s) {
        if (picture_of(s) == nullptr) {
            wanted_.push_back(s);
        }
    }
    worker_->want_pictures(generation_, wanted_);
}

void SheetPreview::draw(ImDrawList* dl, ImVec2 a, ImVec2 b, double opening_s, float rounding,
                        float scale) {
    dl->AddRectFilled(a, b, kPaper, rounding);
    if (status_ != Status::Ready || !song_ || layout_.systems.empty()) {
        return;
    }
    const double tick = song_->song.tempo.tick_at(first_note_s_ + opening_s);
    const SheetCursor cursor = sheet_cursor_at(map_, tick);
    top_ = std::max(0, cursor.system);
    const float space = options_.staff_space_px * scale;
    const float pad_x = std::max(0.0f, ((b.x - a.x) - options_.width_px * scale) * 0.5f);
    const float x = a.x + pad_x;
    float y = a.y + space * kGapSpaces * 0.5f;
    dl->PushClipRect(a, b, true);
    for (int s = top_; s < static_cast<int>(layout_.systems.size()); ++s) {
        const SheetSystem& sys = layout_.systems[static_cast<std::size_t>(s)];
        const float h = sys.height * scale;
        if (y >= b.y || (s > top_ && y + h > b.y)) {
            break;
        }
        if (const Picture* p = picture_of(s)) {
            const ImVec2 lo{x, y};
            const ImVec2 hi{x + static_cast<float>(p->width) * scale,
                            y + static_cast<float>(p->height) * scale};
            dl->AddImage(texture_ref(p->base), lo, hi);
            dl->AddImage(texture_ref(p->notes), lo, hi, {0.0f, 0.0f}, {1.0f, 1.0f}, kInk);
        }
        if (s == cursor.system) {
            const float cx = x + cursor.x * scale;
            const float reach = space * kCursorReachSpaces;
            dl->AddRectFilled({cx - space * kCursorSpaces * 0.5f, y + sys.staff_top * scale - reach},
                              {cx + space * kCursorSpaces * 0.5f,
                               y + sys.staff_bottom * scale + reach},
                              kCursor, space * kCursorSpaces * 0.5f);
        }
        y += h + space * kGapSpaces;
    }
    dl->PopClipRect();
}

} // namespace etude::app
