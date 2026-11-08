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

void LibraryScreen::draw_get_details(Kit& k, UiModel& model, LibraryPort& port, const Box& area) {
    const CatalogueSnapshot* snap = port.catalogue_songs();
    if (snap == nullptr || get_entry_ >= snap->entries.size()) {
        over_ = Over::None;
        return;
    }
    const etude::CatalogueEntry& e = snap->entries[get_entry_];
    const CatalogueInfo& info = catalogue_info(e.source);
    // The card is the preview: large, growing with the window.
    const float margin = k.s(lib_tok::kSidePad);
    const float w = std::min(area.w() - 2.0f * margin,
                             std::max(k.s(lib_tok::kDetailsW), area.w() * lib_tok::kGetCardShareW));
    const float h = std::min(area.h() - 2.0f * margin,
                             std::max(k.s(lib_tok::kDetailsH), area.h() * lib_tok::kGetCardShareH));
    // On whole pixels, so the icons on it sit centred.
    const float x = std::floor(area.cx() - w * 0.5f);
    const float y = std::floor(area.y0 + std::max(margin, (area.h() - h) * 0.5f));
    const float pad = k.s(tok::kS4);
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const float title_size = k.s(tok::kTitle);
    const float label = k.s(tok::kLabel);
    const float caption = k.s(tok::kCaption);
    const float radius = k.s(tok::kRadiusSheet);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    sheet_card(k, dl, {x, y}, {x + w, y + h}, radius, kScreenBg);
    k.toast_avoid({x, y, w, h});

    // The bands the preview leaves clear: the title's at the top (the notes start under it), the
    // controls' above the keys (the notes reach it as they reach the keys).
    const TitleLines& lines =
        title_lines(k.bold, title_size, e.title, w - 2.0f * pad - hit - gap, w - 2.0f * pad - hit);
    const bool credit = !e.credit.empty();
    const float top_band = pad + title_size * 1.25f * static_cast<float>(lines.count) +
                           label * 1.4f + caption * 1.4f * (credit ? 2.0f : 1.0f) + gap;
    const float bottom_band = hit + 2.0f * gap;
    const ImVec2 pa{x, y + top_band};
    const ImVec2 pb{x + w, y + h};

    // The preview: falling notes over the keys, or the sheet music; a line while the song comes.
    const CatalogueFile file = port.catalogue_file(get_entry_);
    const SongOpening* opening = port.preview();
    float band_top = pb.y - bottom_band;
    const auto centred = [&](std::string_view line, ImVec4 colour) {
        const FitText cut = fit(k.regular, label, line, w - 2.0f * pad);
        label_at(k, dl, k.regular, label, {x + pad, pa.y, x + w - pad, band_top},
                 LabelAlign::Centre, colour, cut.c_str());
    };
    if (opening == nullptr || k.library_preview == nullptr) {
        FitText waiting;
        if (file_busy(file)) {
            if (file.progress > 0.0f) {
                waiting.format("Fetching the song\xE2\x80\xA6 %d%%",
                               static_cast<int>(std::lround(file.progress * 100.0f)));
            } else {
                waiting.assign("Fetching the song\xE2\x80\xA6");
            }
        } else if (file.state == CatalogueFile::State::Failed) {
            waiting.assign(port.catalogue_file_reason(get_entry_));
        } else if (port.preview_failed()) {
            waiting.assign("No notes to show");
        } else {
            waiting.assign("Reading the opening\xE2\x80\xA6");
        }
        centred(waiting.view(), col::kText2);
    } else {
        LivePreview& lp = *k.library_preview;
        lp.set_opening(opening, port.preview_keys());
        if (preview_sheet_ && k.library_sheet != nullptr) {
            const double t = lp.run_opening(k.now);
            SheetPreview& sheet = *k.library_sheet;
            const ImVec2 sa{x + pad, pa.y};
            const ImVec2 sb{x + w - pad, band_top};
            const float space = std::clamp((sb.y - sa.y) / lib_tok::kPreviewSheetSpaces,
                                           k.s(lib_tok::kPreviewSheetLeastSpace),
                                           k.s(lib_tok::kPreviewSheetMostSpace));
            sheet.want(port.preview_song(), (sb.x - sa.x - 2.0f * gap) * k.fb_scale,
                       space * k.fb_scale);
            sheet.update();
            audit::live(sa, sb);
            sheet.draw(dl, sa, sb, t, k.s(tok::kRadiusMd), 1.0f / k.fb_scale);
            if (sheet.status() == SheetPreview::Status::NoSheet) {
                FitText why;
                why.format("No sheet music: %.*s", static_cast<int>(sheet.reason().size()),
                           sheet.reason().data());
                centred(why.view(), ImVec4{0.25f, 0.25f, 0.28f, 1.0f});
            } else if (sheet.status() != SheetPreview::Status::Ready) {
                centred("Engraving the score\xE2\x80\xA6", ImVec4{0.25f, 0.25f, 0.28f, 1.0f});
            }
        } else {
            band_top = lp.draw_banded(k, model.look, pa, pb, bottom_band, radius);
        }
        for (const LivePreview::Struck& s : lp.struck()) {
            if (preview_sound_ || s.velocity == 0) {
                port.preview_note(s.key, s.velocity);
            }
        }
        lp.clear_struck();
    }
    // Soft shades behind the words and the controls, fading into the preview.
    {
        const ImU32 dark = u32(ImVec4{kScreenBg.x, kScreenBg.y, kScreenBg.z, 0.9f});
        const ImU32 clear = u32(ImVec4{kScreenBg.x, kScreenBg.y, kScreenBg.z, 0.0f});
        const float fade = k.s(tok::kS4);
        if (!preview_sheet_) {
            dl->AddRectFilledMultiColor({x + radius * 0.5f, pa.y},
                                        {x + w - radius * 0.5f, pa.y + fade}, dark, dark, clear,
                                        clear);
        }
        dl->AddRectFilledMultiColor({x, band_top}, {x + w, band_top + bottom_band * 0.5f}, clear,
                                    clear, dark, dark);
        dl->AddRectFilled({x, band_top + bottom_band * 0.5f}, {x + w, band_top + bottom_band}, dark);
    }

    // The top: the title, the composer, where it is from and its licence (and the credit); close.
    float cy = y + pad;
    const float tx = x + pad;
    const float text_r = x + w - pad;
    for (int i = 0; i < lines.count; ++i) {
        label_at(k, dl, k.bold, title_size, {tx, cy, text_r - hit, cy + title_size * 1.25f},
                 LabelAlign::Left, col::kText, lines.line[i].c_str());
        cy += title_size * 1.25f;
    }
    {
        FitText sub;
        sub.assign(e.composer.empty() ? std::string_view("Composer not known")
                                      : std::string_view(e.composer));
        if (!e.detail.empty()) {
            sub.append(" \xC2\xB7 ");
            sub.append(e.detail);
        }
        const FitText cut = fit(k.regular, label, sub.view(), w - 2.0f * pad - hit);
        label_at(k, dl, k.regular, label, {tx, cy, text_r - hit, cy + label * 1.4f},
                 LabelAlign::Left, col::kText2, cut.c_str());
        cy += label * 1.4f;
        FitText from;
        from.format("From %.*s \xC2\xB7 %s", static_cast<int>(info.name.size()), info.name.data(),
                    e.licence.c_str());
        const FitText from_cut = fit(k.regular, caption, from.view(), w - 2.0f * pad);
        label_at(k, dl, k.regular, caption, {tx, cy, text_r, cy + caption * 1.4f},
                 LabelAlign::Left, col::kText3, from_cut.c_str());
        cy += caption * 1.4f;
        if (credit) {
            FitText by;
            by.format("Credit: %s", e.credit.c_str());
            const FitText by_cut = fit(k.regular, caption, by.view(), w - 2.0f * pad);
            label_at(k, dl, k.regular, caption, {tx, cy, text_r, cy + caption * 1.4f},
                     LabelAlign::Left, col::kText3, by_cut.c_str());
        }
    }
    if (round_button(k, "##get-close", icon::kClose,
                     {x + w - gap - hit * 0.5f, y + gap + hit * 0.5f}, false, Idle::Fill)) {
        over_ = Over::None;
        return;
    }

    // The band above the keys: Download (its progress, a cancel, a retry) or Play once the song is
    // in the library, and Credits, at the left; the sound and the sheet music at the right.
    const float by = band_top + gap;
    const float right_x = x + w - pad;
    const ImVec2 sheet_c{right_x - hit * 0.5f, by + hit * 0.5f};
    const ImVec2 sound_c{sheet_c.x - hit - gap, sheet_c.y};
    if (round_button(k, "##details-sheet", lib_icon::kSheet, sheet_c, preview_sheet_, Idle::Fill)) {
        preview_sheet_ = !preview_sheet_;
    }
    long_press_tip(k, preview_sheet_ ? "Falling notes" : "Sheet music");
    if (round_button(k, "##details-sound", preview_sound_ ? icon::kSoundBoth : icon::kMute, sound_c,
                     preview_sound_, Idle::Fill)) {
        preview_sound_ = !preview_sound_;
        if (!preview_sound_ && k.library_preview != nullptr) {
            const std::bitset<128>& on = k.library_preview->sounding();
            for (std::size_t key = 0; key < on.size(); ++key) {
                if (on.test(key)) {
                    port.preview_note(static_cast<std::uint8_t>(key), 0);
                }
            }
        }
    }
    long_press_tip(k, preview_sound_ ? "Sound off" : "Sound on");
    const float controls_right = sound_c.x - hit * 0.5f - gap;
    {
        const std::optional<std::size_t> have = port.downloaded_song(get_entry_);
        const bool busy = file_busy(file) && file.keep;
        const bool failed = file.state == CatalogueFile::State::Failed;
        const bool adding = !have && file.state == CatalogueFile::State::Kept;
        FitText words;
        const char* glyph_text = icon::kLoad;
        if (have) {
            words.assign("Play");
            glyph_text = icon::kPlay;
        } else if (busy) {
            if (file.progress >= 0.0f) {
                words.format("Cancel %d%%", static_cast<int>(std::lround(file.progress * 100.0f)));
            } else {
                words.assign("Cancel");
            }
            glyph_text = icon::kClose;
        } else if (adding) {
            words.assign("Adding\xE2\x80\xA6");
        } else if (failed && file.keep) {
            words.assign("Try again");
            glyph_text = icon::kReset;
        } else {
            words.assign("Download");
        }
        const float bw = std::min(controls_right - tx,
                                  std::max(k.s(lib_tok::kPopoverW) * 0.5f,
                                           chip_width(k, glyph_text, words.view(), nullptr) +
                                               2.0f * k.s(tok::kS3)));
        if (action_button(k, "##get-download", glyph_text, words.c_str(), {tx, by}, {bw, hit},
                          !adding, !busy)) {
            if (have) {
                port.open(*have);
                over_ = Over::None;
                return;
            }
            if (busy) {
                port.cancel_download(get_entry_);
            } else {
                port.download(get_entry_);
            }
        }
        if (busy) {
            const float bar = k.s(lib_tok::kGetProgress);
            const float r = k.s(tok::kRadiusControl);
            dl->AddRectFilled({tx + r, by + hit - bar}, {tx + bw - r, by + hit}, u32(kTrack),
                              bar * 0.5f);
            if (file.progress > 0.0f) {
                dl->AddRectFilled(
                    {tx + r, by + hit - bar},
                    {tx + r + (bw - 2.0f * r) * std::min(file.progress, 1.0f), by + hit},
                    u32(col::kAccent), bar * 0.5f);
            }
        }
        float bx = tx + bw + gap;
        // Credits: in words where they fit, else its icon.
        const float cw_words = chip_width(k, lib_icon::kInfo, "Credits", nullptr);
        const bool words_fit = bx + cw_words <= controls_right;
        const float cw = words_fit ? cw_words : hit;
        if (bx + cw <= controls_right) {
            if (text_chip(k, "##get-about", lib_icon::kInfo, words_fit ? "Credits" : "", nullptr,
                          {bx, by}, cw, false)) {
                credits_back_ = Over::Get;
                over_ = Over::Credits;
                return;
            }
            long_press_tip(k, "Credits");
            bx += cw + gap;
        }
        if (failed && bx < controls_right) {
            const FitText why = fit(k.regular, caption, port.catalogue_file_reason(get_entry_),
                                    controls_right - bx);
            label_at(k, dl, k.regular, caption, {bx, by, controls_right, by + hit},
                     LabelAlign::Left, col::kWarning, why.c_str());
        }
    }
}

void LibraryScreen::draw_credits(Kit& k, UiModel& model, LibraryPort& port, const Box& area) {
    const float margin = k.s(lib_tok::kSidePad);
    const float w = std::min(k.s(lib_tok::kCreditsW), area.w() - 2.0f * margin);
    const float pad = k.s(tok::kS5);
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const float body = k.s(tok::kBody);
    const float label = k.s(tok::kLabel);
    const float caption = k.s(tok::kCaption);
    const float inner = w - 2.0f * pad;
    // Each catalogue: its name and how it stands, its licence and credit, what it is.
    const float block = body * 1.4f + label * 1.4f + caption * 1.4f + k.s(tok::kS4);
    int listed = 0;
    for (const CatalogueInfo& info : catalogue_infos()) {
        listed += info.enabled ? 1 : 0;
    }
    const float content = hit + gap + static_cast<float>(listed) * block + hit +
                          caption * 1.6f;
    const float h = std::min(area.h() - 2.0f * margin, content + 2.0f * pad);
    // On whole pixels, so the icons on it sit centred.
    const float x = std::floor(area.cx() - w * 0.5f);
    const float y = std::floor(area.y0 + std::max(margin, (area.h() - h) * 0.5f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    sheet_card(k, dl, {x, y}, {x + w, y + h}, k.s(tok::kRadiusSheet), col::kSurface);
    k.toast_avoid({x, y, w, h});
    const float title_size = k.s(tok::kTitle);
    label_at(k, dl, k.bold, title_size, {x + pad, y + pad - gap, x + w - hit, y + pad - gap + hit},
             LabelAlign::Left, col::kText, "Free song catalogues");
    if (round_button(k, "##credits-close", icon::kClose,
                     {x + w - k.s(tok::kS2) - hit * 0.5f, y + k.s(tok::kS2) + hit * 0.5f}, false,
                     Idle::Fill)) {
        over_ = credits_back_;
        return;
    }
    // What does not fit scrolls.
    const float top = y + pad + hit;
    ImGui::SetCursorScreenPos({x, top});
    ImGui::BeginChild("##credits-list", {w, y + h - pad * 0.5f - top}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float scroll = ImGui::GetScrollY();
    float cy = origin.y + gap - scroll;
    const float tx = origin.x + pad;
    for (const CatalogueInfo& info : catalogue_infos()) {
        if (!info.enabled) {
            continue;
        }
        const CatalogueIndexState& st = port.catalogue_state(info.id);
        cdl->AddRectFilled({tx - k.s(tok::kS3), cy + k.s(tok::kS1)},
                           {tx - k.s(tok::kS3) + k.s(lib_tok::kBand), cy + block - k.s(tok::kS4)},
                           u32(catalogue_colour(info.id)), k.s(lib_tok::kBand) * 0.5f);
        FitText name;
        name.assign(info.name);
        const float name_w = text_size(k.bold, body, name.c_str()).x;
        const Box name_line{tx, cy, tx + name_w, cy + body * 1.4f};
        label_at(k, cdl, k.bold, body, name_line, LabelAlign::Left, col::kText, name.c_str());
        FitText status;
        switch (st.kind) {
        case CatalogueIndexState::Kind::Ready:
            status.format("%zu songs", st.songs);
            break;
        case CatalogueIndexState::Kind::Fetching:
            status.format("Reading its listing\xE2\x80\xA6 %zu", st.songs);
            break;
        case CatalogueIndexState::Kind::Failed:
            status.assign(st.reason);
            break;
        case CatalogueIndexState::Kind::Reading:
            status.assign("Reading\xE2\x80\xA6");
            break;
        case CatalogueIndexState::Kind::None:
            status.assign(model.app.get_songs_online ? "Not read yet" : "Offline");
            break;
        }
        const float sx = name_line.x1 + k.s(tok::kS3);
        const FitText status_cut = fit(k.regular, caption, status.view(), tx + inner - sx);
        label_at(k, cdl, k.regular, caption, {sx, name_line.y0, tx + inner, name_line.y1},
                 LabelAlign::Left,
                 st.kind == CatalogueIndexState::Kind::Failed ? col::kWarning : col::kText3,
                 status_cut.c_str());
        cy += body * 1.4f;
        FitText lic;
        lic.assign(info.licence);
        if (!info.credit.empty()) {
            lic.append(" \xC2\xB7 Credit: ");
            lic.append(info.credit);
        }
        const FitText lic_cut = fit(k.regular, label, lic.view(), inner);
        label_at(k, cdl, k.regular, label, {tx, cy, tx + inner, cy + label * 1.4f},
                 LabelAlign::Left, col::kText2, lic_cut.c_str());
        cy += label * 1.4f;
        const FitText about = fit(k.regular, caption, info.about, inner);
        label_at(k, cdl, k.regular, caption, {tx, cy, tx + inner, cy + caption * 1.4f},
                 LabelAlign::Left, col::kText3, about.c_str());
        cy += caption * 1.4f + k.s(tok::kS4);
    }
    // Online: on or off, and the listings read again.
    catalogue_sweep::drawn(Setting::GetSongsOnline);
    bool& online = model.app.get_songs_online;
    const float ow = chip_width(k, lib_icon::kGlobe, "Search online", nullptr);
    ImGui::SetCursorScreenPos({tx, cy});
    if (text_chip(k, "##credits-online", lib_icon::kGlobe, "Search online", nullptr, {tx, cy}, ow,
                  online)) {
        online = !online;
    }
    if (online) {
        const float rw = chip_width(k, icon::kReset, "Read the listings again", nullptr);
        if (tx + ow + gap + rw <= tx + inner &&
            text_chip(k, "##credits-refresh", icon::kReset, "Read the listings again", nullptr,
                      {tx + ow + gap, cy}, rw, false)) {
            port.refresh_catalogues();
        }
    }
    cy += hit + gap;
    const FitText note_cut = fit(k.regular, caption,
                                 online ? "On: the app reaches these sites alone, when you search"
                                        : "Off: nothing is fetched; saved listings are still searched",
                                 inner);
    label_at(k, cdl, k.regular, caption, {tx, cy, tx + inner, cy + caption * 1.6f},
             LabelAlign::Left, col::kText3, note_cut.c_str());
    cy += caption * 1.6f;
    ImGui::SetCursorScreenPos({origin.x, cy + gap});
    ImGui::Dummy({1.0f, 1.0f});
    drag_scroll(k, true);
    ImGui::EndChild();
}

} // namespace etude::app::ui
