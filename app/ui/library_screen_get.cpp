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
#include <cstdio>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace etude::app::ui {

using namespace lib_shared;

namespace {

// A download's progress as an arc round a round button's face, from the top, clockwise.
void progress_arc(const Kit& k, ImDrawList* dl, ImVec2 c, float progress) {
    const float r = k.s(tok::kFace) * 0.5f + k.s(lib_tok::kRing) * 0.5f + k.s(tok::kHairline);
    const float thick = k.s(lib_tok::kRing);
    dl->AddCircle(c, r, u32(kTrack), 40, thick);
    if (progress > 0.0f) {
        const float start = -IM_PI * 0.5f;
        dl->PathArcTo(c, r, start, start + 2.0f * IM_PI * std::min(progress, 1.0f), 40);
        dl->PathStroke(u32(col::kAccent), 0, thick);
    }
}

} // namespace

void LibraryScreen::open_get(LibraryPort& port, std::uint32_t entry) {
    const CatalogueSnapshot* snap = port.catalogue_songs();
    if (snap == nullptr || entry >= snap->keys.size()) {
        return;
    }
    get_key_ = snap->keys[entry];
    get_entry_ = entry;
    over_ = Over::Get;
    // The preview plays at once, silent until its sound is turned on.
    previewing_ = true;
    preview_sound_ = false;
    port.request_catalogue_preview(entry);
}

bool LibraryScreen::find_held_entry(LibraryPort& port) {
    const CatalogueSnapshot* snap = port.catalogue_songs();
    if (snap == nullptr || get_key_ == 0) {
        return false;
    }
    if (get_entry_ < snap->keys.size() && snap->keys[get_entry_] == get_key_) {
        return true;
    }
    for (std::uint32_t i = 0; i < snap->keys.size(); ++i) {
        if (snap->keys[i] == get_key_) {
            get_entry_ = i;
            return true;
        }
    }
    return false;
}

float LibraryScreen::draw_get_songs(Kit& k, UiModel& model, LibraryPort& port, ImVec2 at,
                                    float width) {
    get_rows_ = 0;
    LibraryCatalogue& cat = port.catalogue();
    if (!cat.searching()) {
        return 0.0f; // before any typing the library shows its own songs alone
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = k.s(lib_tok::kSidePad);
    const float hit = k.s(tok::kHit);
    const float row_h = k.s(lib_tok::kGetRowH);
    const float gap = k.s(lib_tok::kGetRowGap);
    const float x0 = at.x + pad;
    const float row_w = width - 2.0f * pad;
    const float label = k.s(tok::kLabel);
    const float caption = k.s(tok::kCaption);
    const ImVec2 clip_lo = dl->GetClipRectMin();
    const ImVec2 clip_hi = dl->GetClipRectMax();
    const auto in_view = [&](float top, float bottom) { return bottom > clip_lo.y && top < clip_hi.y; };
    float y = std::round(at.y + k.s(tok::kS3));
    const std::span<const CatalogueHit> hits = port.catalogue_hits();
    const CatalogueSnapshot* snap = port.catalogue_songs();
    const bool online = model.app.get_songs_online;
    const bool current = port.catalogue_hits_query() == cat.query();

    // The hits as rows: the versions of one piece are one row (with the setting on), shown as the
    // version chosen for it, else the one the search found first. Made again only when the hits,
    // the snapshot, the setting or a choice changed.
    if (snap != nullptr) {
        const CatalogueVersions& vers = snap->versions;
        const bool grouped = cat.group_versions() && vers.group_of.size() == snap->entries.size();
        const GetListKey key{port.catalogue_hits_serial(), snap, grouped, get_choices_};
        if (!(key == get_list_key_)) {
            get_list_key_ = key;
            get_list_.clear();
            for (const CatalogueHit& h : hits) {
                if (h.entry >= snap->entries.size()) {
                    continue;
                }
                const std::uint32_t g = grouped ? vers.group_of[h.entry] : CatalogueVersions::kNone;
                if (g == CatalogueVersions::kNone) {
                    get_list_.push_back({h.entry, CatalogueVersions::kNone});
                } else if (std::none_of(get_list_.begin(), get_list_.end(),
                                        [&](const GetRow& r) { return r.group == g; })) {
                    get_list_.push_back({get_shown(*snap, g, h.entry), g});
                }
            }
        }
    } else {
        get_list_.clear();
        get_list_key_ = {};
    }

    // The header: Get songs, how many (or that it is searching), the credits.
    if (in_view(y, y + hit)) {
        const float icon_size = k.s(tok::kIconSm);
        glyph(k, dl, lib_icon::kGlobe, {x0 + icon_size * 0.5f, y + hit * 0.5f}, icon_size,
              col::kText2);
        float hx = x0 + icon_size + k.s(tok::kS2);
        const float name_w = text_size(k.bold, label, "Get songs").x;
        label_at(k, dl, k.bold, label, {hx, y, hx + name_w, y + hit}, LabelAlign::Left,
                 col::kText2, "Get songs");
        hx += name_w + k.s(tok::kS2);
        FitText count;
        if (online || (snap != nullptr && !snap->entries.empty())) {
            if (!current) {
                count.assign("Searching\xE2\x80\xA6");
            } else {
                count.format("%zu", get_list_.size());
            }
        }
        label_at(k, dl, k.regular, label, {hx, y, at.x + width - pad - hit, y + hit},
                 LabelAlign::Left, col::kText3, count.c_str());
    }
    if (round_button(k, "##get-credits", lib_icon::kInfo, {at.x + width - pad - hit * 0.5f, y + hit * 0.5f},
                     false, Idle::None)) {
        credits_back_ = Over::None;
        over_ = Over::Credits;
    }
    long_press_tip(k, "About the catalogues");
    y += hit + k.s(tok::kHitGap);

    // Offline: what turning them on does, and the switch.
    if (!online) {
        catalogue_sweep::drawn(Setting::GetSongsOnline);
        // "Search free songs from A, B and C", the catalogues that are on.
        FitText line;
        line.assign("Search free songs from ");
        std::size_t on = 0;
        for (const CatalogueInfo& info : catalogue_infos()) {
            on += info.enabled ? 1 : 0;
        }
        std::size_t named = 0;
        for (const CatalogueInfo& info : catalogue_infos()) {
            if (!info.enabled) {
                continue;
            }
            if (named > 0) {
                line.append(named + 1 == on ? " and " : ", ");
            }
            line.append(info.name);
            ++named;
        }
        const TitleLines& lines = title_lines(k.regular, label, line.c_str(), row_w, row_w);
        for (int i = 0; i < lines.count; ++i) {
            label_at(k, dl, k.regular, label, {x0, y, x0 + row_w, y + label * 1.35f},
                     LabelAlign::Left, col::kText2, lines.line[i].c_str());
            y += label * 1.35f;
        }
        y += k.s(tok::kS2);
        const float bw = std::min(row_w, chip_width(k, lib_icon::kGlobe, "Search online", nullptr) +
                                             2.0f * k.s(tok::kS3));
        if (action_button(k, "##get-online", lib_icon::kGlobe, "Search online", {x0, y}, {bw, hit},
                          true, true)) {
            model.app.get_songs_online = true;
        }
        y += hit + k.s(tok::kS2);
        const char* note_line = "Only these sites are reached, and only once this is on.";
        const FitText cut = fit(k.regular, caption, note_line, row_w);
        label_at(k, dl, k.regular, caption, {x0, y, x0 + row_w, y + caption * 1.6f},
                 LabelAlign::Left, col::kText3, cut.c_str());
        y += caption * 1.6f;
    } else {
        // A catalogue being read or that failed says so, in a line each.
        for (const CatalogueInfo& info : catalogue_infos()) {
            if (!info.enabled) {
                continue;
            }
            const CatalogueIndexState& st = port.catalogue_state(info.id);
            FitText line;
            if (st.kind == CatalogueIndexState::Kind::Fetching) {
                if (st.songs > 0) {
                    line.format("%.*s: reading its listing\xE2\x80\xA6 %zu songs so far",
                                static_cast<int>(info.name.size()), info.name.data(), st.songs);
                } else {
                    line.format("%.*s: reading its listing\xE2\x80\xA6",
                                static_cast<int>(info.name.size()), info.name.data());
                }
            } else if (st.kind == CatalogueIndexState::Kind::Failed) {
                line.format("%.*s: %s", static_cast<int>(info.name.size()), info.name.data(),
                            st.reason.c_str());
            } else {
                continue;
            }
            const FitText cut = fit(k.regular, caption, line.view(), row_w);
            label_at(k, dl, k.regular, caption, {x0, y, x0 + row_w, y + caption * 1.6f},
                     LabelAlign::Left, col::kText3, cut.c_str());
            y += caption * 1.6f;
        }
    }

    // The matches.
    if (snap != nullptr && current && get_list_.empty() && !snap->entries.empty()) {
        FitText line;
        line.format("No free songs match \xE2\x80\x9C%s\xE2\x80\x9D", cat.query().c_str());
        const FitText cut = fit(k.regular, label, line.view(), row_w);
        label_at(k, dl, k.regular, label, {x0, y, x0 + row_w, y + label * 1.6f + k.s(tok::kS1)},
                 LabelAlign::Left, col::kText2, cut.c_str());
        y += label * 1.6f + k.s(tok::kS1);
    }
    if (snap != nullptr) {
        ImGui::PushID("##get");
        for (const GetRow& r : get_list_) {
            y = std::round(y);
            // A piece with versions has a row of chips under its text.
            const float chips_h = r.group != CatalogueVersions::kNone ? k.s(lib_tok::kGetChipsH) : 0.0f;
            if (in_view(y, y + row_h + chips_h)) {
                draw_get_row(k, port, r.entry, r.group, get_rows_, {x0, y},
                             {x0 + row_w, y + row_h + chips_h}, chips_h);
            }
            ++get_rows_;
            y += row_h + chips_h + gap;
        }
        ImGui::PopID();
    }
    return y - at.y + k.s(tok::kS2);
}

std::uint32_t LibraryScreen::get_shown(const CatalogueSnapshot& snap, std::uint32_t group,
                                       std::uint32_t matched) const {
    const std::span<const std::uint32_t> members = snap.versions.of(group);
    for (const std::uint32_t m : members) {
        if (get_choice_.count(snap.keys[m]) != 0) {
            return m;
        }
    }
    return matched;
}

void LibraryScreen::draw_get_chips(Kit& k, LibraryPort& port, std::uint32_t group,
                                   std::uint32_t shown, int row, ImVec2 at, float width) {
    const CatalogueSnapshot& snap = *port.catalogue_songs();
    const CatalogueVersions& vers = snap.versions;
    const std::span<const std::uint32_t> members = vers.of(group);
    const std::size_t n = std::min(members.size(), kMaxVersions);
    std::array<float, kMaxVersions> widths{};
    for (std::size_t i = 0; i < n; ++i) {
        ChipWidthCache::Widths& w =
            get_chip_widths_.at(members[i], snap.entries.size(), snap.version, k.u);
        if (w.name < 0.0f) {
            w.name = chip_width(k, nullptr, vers.name_of(members[i]));
        }
        widths[i] = w.name;
    }
    if (get_chip_widths_.more_w < 0.0f || get_chip_widths_.more_unit != k.u) {
        get_chip_widths_.more_w = chip_width(k, nullptr, "+9");
        get_chip_widths_.more_unit = k.u;
    }
    const std::size_t chosen = vers.member_of[shown];
    const VersionPlan plan = plan_version_chips({widths.data(), n}, {widths.data(), n}, chosen,
                                                width, k.s(tok::kHitGap), get_chip_widths_.more_w, false);
    const float gap = k.s(tok::kHitGap);
    float x = at.x;
    std::optional<std::size_t> tapped;
    for (std::size_t i = plan.first; i < plan.first + plan.count; ++i) {
        char id[40];
        std::snprintf(id, sizeof id, "##get-ver-%d-%zu", row, i);
        ChipOpts o;
        o.on = i == chosen;
        if (chip(k, id, vers.name_of(members[i]), {x, at.y}, o)) {
            tapped = i;
        }
        x += widths[i] + gap;
    }
    if (plan.more) {
        FitText more;
        more.format("+%zu", n - plan.count);
        char id[40];
        std::snprintf(id, sizeof id, "##get-ver-%d-more", row);
        // The next version after those shown (round again from the first).
        if (chip(k, id, more.view(), {x, at.y}, {})) {
            tapped = plan.first + plan.count < n ? plan.first + plan.count : 0;
        }
    }
    if (tapped) {
        // One choice for the piece: the others' go, and the rows are made again.
        for (const std::uint32_t m : members) {
            get_choice_.erase(snap.keys[m]);
        }
        get_choice_.insert(snap.keys[members[*tapped]]);
        ++get_choices_;
    }
}

void LibraryScreen::draw_get_row(Kit& k, LibraryPort& port, std::uint32_t entry,
                                 std::uint32_t group, int row, ImVec2 a, ImVec2 b,
                                 float chips_h) {
    const etude::CatalogueEntry& e = port.catalogue_songs()->entries[entry];
    const bool versions = group != CatalogueVersions::kNone;
    // The text and the button sit in the row's top; the chips are under them.
    const ImVec2 full_b = b;
    b.y -= chips_h;
    const CatalogueInfo& info = catalogue_info(e.source);
    ImGui::PushID(static_cast<int>(entry));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = k.s(tok::kRadiusControl);
    const float hit = k.s(tok::kHit);
    const float cy = (a.y + b.y) * 0.5f;
    const CatalogueFile f = port.catalogue_file(entry);
    const std::optional<std::size_t> have = port.downloaded_song(entry);
    const bool kept = have.has_value() || f.state == CatalogueFile::State::Kept;
    const bool busy = file_busy(f) && f.keep;
    const bool failed = f.state == CatalogueFile::State::Failed;
    char action_id[24];
    char card_id[24];
    std::snprintf(action_id, sizeof action_id, "##get-action-%d", row);
    std::snprintf(card_id, sizeof card_id, "##get-card-%d", row);
    // The button at the right end; the catalogue and its licence before it where the row is wide.
    const ImVec2 bc{b.x - k.s(tok::kS2) - hit * 0.5f, cy};
    const float text_x = a.x + k.s(lib_tok::kBand) + k.s(tok::kS3);
    float text_right = bc.x - hit * 0.5f - k.s(tok::kS2);
    const float source_w = k.s(lib_tok::kGetSourceW);
    const float tap_right = text_right; // the card's own tap area ends a dead gap before the button
    const bool source = text_right - text_x - source_w - k.s(tok::kS3) >= k.s(lib_tok::kGetLeastTextW);
    if (source) {
        text_right -= source_w + k.s(tok::kS3);
    }
    const ImVec2 lo = dl->GetClipRectMin();
    const ImVec2 hi = dl->GetClipRectMax();
    if (cy > lo.y && cy < hi.y && a.x >= lo.x) {
        note(card_id, a, {tap_right, b.y}, InkKind::Target);
    }
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    const char* action_glyph = kept ? icon::kCheck : busy ? icon::kClose : failed ? icon::kReset
                                                                             : icon::kLoad;
    if (round_button(k, action_id, action_glyph, bc, kept, Idle::Fill)) {
        if (have) {
            port.open(*have);
        } else if (busy) {
            port.cancel_download(entry);
        } else if (!kept) {
            port.download(entry);
        }
    }
    long_press_tip(k, have ? "Play" : kept ? "In your library" : busy ? "Cancel the download"
                                                            : failed ? "Try again"
                                                                     : "Download");
    if (busy) {
        progress_arc(k, dl, bc, std::max(0.0f, f.progress));
    }
    // The chips of the piece's versions, under the text: a tap chooses the one the row shows.
    if (versions) {
        draw_get_chips(k, port, group, entry, row, {text_x, b.y + k.s(tok::kHitGap)},
                       full_b.x - k.s(tok::kS2) - text_x);
    }
    dl->ChannelsSetCurrent(0);
    const bool pressed = press_area(k, card_id, {a.x, a.y, tap_right, b.y});
    const State st = item_state(k, over_ == Over::Get && get_key_ != 0 &&
                                       port.catalogue_songs()->keys[entry] == get_key_);
    card_face(k, dl, a, full_b, radius);
    dl->AddRectFilled({a.x, a.y + k.s(tok::kS3)},
                      {a.x + k.s(lib_tok::kBand), full_b.y - k.s(tok::kS3)},
                      u32(catalogue_colour(e.source)), k.s(lib_tok::kBand) * 0.5f);
    draw_state(k, dl, card_id, {a.x, a.y, full_b.x, full_b.y}, {a.x, a.y, full_b.x, full_b.y}, st,
               Idle::None, radius);
    // The title over the composer and what it is, or why the download failed.
    const float ts = k.s(tok::kBody);
    const float cs = k.s(tok::kCaption);
    const float text_w = std::max(0.0f, text_right - text_x);
    const FitText title = fit(k.bold, ts,
                              versions ? std::string_view(port.catalogue_songs()->versions.pieces[group])
                                       : std::string_view(e.title),
                              text_w);
    FitText sub;
    if (failed) {
        sub.assign(port.catalogue_file_reason(entry));
    } else {
        sub.assign(e.composer.empty() ? std::string_view("Composer not known")
                                      : std::string_view(e.composer));
        if (!e.detail.empty()) {
            sub.append(" \xC2\xB7 ");
            sub.append(e.detail);
        }
        if (!source) {
            sub.append(" \xC2\xB7 ");
            sub.append(info.name);
        }
    }
    const FitText sub_cut = fit(k.regular, cs, sub.view(), text_w);
    const StackLine sub_line{k.regular, cs, failed ? col::kWarning : col::kText2, sub_cut.c_str()};
    label_stack(k, dl, {text_x, a.y, text_right, b.y}, {k.bold, ts, col::kText, title.c_str()},
                &sub_line);
    if (source) {
        const float sx = text_right + k.s(tok::kS3);
        const FitText name = fit(k.regular, cs, info.name, source_w);
        const FitText lic = fit(k.regular, cs, e.licence, source_w);
        const StackLine lic_line{k.regular, cs, col::kText3, lic.c_str()};
        label_stack(k, dl, {sx, a.y, sx + source_w, b.y}, {k.regular, cs, col::kText2, name.c_str()},
                    &lic_line);
    }
    // The download's progress along the row's bottom.
    if (busy) {
        const float bar = k.s(lib_tok::kGetProgress);
        const float x1 = full_b.x - radius;
        const float x0 = a.x + radius;
        dl->AddRectFilled({x0, full_b.y - bar}, {x1, full_b.y}, u32(kTrack), bar * 0.5f);
        if (f.progress > 0.0f) {
            dl->AddRectFilled({x0, full_b.y - bar},
                              {x0 + (x1 - x0) * std::min(f.progress, 1.0f), full_b.y},
                              u32(col::kAccent), bar * 0.5f);
        }
    }
    dl->ChannelsMerge();
    const Press press = press_of(pressed);
    if (press.tap || press.right) {
        open_get(port, entry);
    }
    ImGui::PopID();
}

} // namespace etude::app::ui
