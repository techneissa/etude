// SPDX-License-Identifier: GPL-3.0-or-later
#include "add_songs_sources.hpp"

#include "add_songs_layout.hpp"
#include "icons.hpp"

#include "etude/network_share.hpp"

#include <algorithm>
#include <cstdio>

namespace etude::app::ui {

namespace {

Box box_at(ImVec2 origin, const Rect& r) {
    return {origin.x + r.x, origin.y + r.y, origin.x + r.right(), origin.y + r.bottom()};
}

// "840 B", "12 KB", "1.4 MB".
std::string size_text(std::uint64_t bytes) {
    char out[24];
    if (bytes < 1000) {
        std::snprintf(out, sizeof out, "%u B", static_cast<unsigned>(bytes));
    } else if (bytes < 1000 * 1000) {
        std::snprintf(out, sizeof out, "%.0f KB", static_cast<double>(bytes) / 1000.0);
    } else {
        std::snprintf(out, sizeof out, "%.1f MB", static_cast<double>(bytes) / 1.0e6);
    }
    return out;
}

std::string songs_text(int n) { return std::to_string(n) + (n == 1 ? " song" : " songs"); }

// Back, at the head's left. True when tapped.
bool back_chip(Kit& k, ImVec2 origin, const SubviewGeometry& g) {
    const float lift = (g.back.h - k.s(tok::kHit)) * 0.5f;
    ChipOpts o;
    o.icon = icon::kBack;
    o.width = g.back.w;
    return chip(k, "##sources_back", "Back", {origin.x + g.back.x, origin.y + g.back.y + lift}, o);
}

void head_title(Kit& k, ImDrawList* dl, ImVec2 origin, const SubviewGeometry& g,
                const std::string& text, ImVec4 ink) {
    const Box slot = box_at(origin, g.title);
    label_at(k, dl, k.bold, k.s(tok::kTitle), slot, LabelAlign::Left, ink,
             fit(k.bold, k.s(tok::kTitle), text, slot.w()).c_str());
}

// `text` wrapped at `wrap` wide from `at`; returns the height it took.
float wrapped_text(Kit& k, ImDrawList* dl, ImVec2 at, float wrap, float size, ImVec4 ink,
                   const char* text) {
    const char* shown = screen_text(text);
    dl->AddText(k.regular, size, at, u32(ink), shown, nullptr, wrap);
    return k.regular->CalcTextSizeA(size, 1.0e6f, wrap, shown).y;
}

// A small label over a bigger value, for an address.
float labelled_line(Kit& k, ImDrawList* dl, ImVec2 at, float w, const char* label,
                    const std::string& value) {
    const float cap = k.s(tok::kCaption) * 1.4f;
    const float line = k.s(tok::kTitle) * 1.4f;
    label_at(k, dl, k.regular, k.s(tok::kCaption), {at.x, at.y, at.x + w, at.y + cap},
             LabelAlign::Left, col::kText2, label);
    label_at(k, dl, k.bold, k.s(tok::kTitle), {at.x, at.y + cap, at.x + w, at.y + cap + line},
             LabelAlign::Left, col::kText, fit(k.bold, k.s(tok::kTitle), value, w).c_str());
    return cap + line + k.s(tok::kS2);
}

} // namespace

std::string network_tile_detail(Kit& k, const NetworkView& view, float room) {
    switch (view.state) {
    case ShareState::Checking:
        return "Checking";
    case ShareState::NotSetUp:
        return "Set up with the Pi";
    case ShareState::Shared:
        break;
    }
    const std::string full = "Shared as etude \u00b7 " + share_windows_address();
    return text_size(k.regular, k.s(tok::kBody), full.c_str()).x <= room ? full
                                                                         : "Shared as etude";
}

std::string usb_tile_detail(const UsbView& v) {
    if (!v.present) {
        return "None plugged in";
    }
    if (v.importing) {
        return "Copying " + std::to_string(v.import_done) + " of " + std::to_string(v.import_total);
    }
    if (v.scanning) {
        return v.label + " · looking";
    }
    return v.label + " · " + songs_text(v.songs) + (v.truncated ? "+" : "");
}

namespace {

// What the sheet says, from `at`, `room` wide; returns how tall it came out.
float network_sheet_text(Kit& k, ImDrawList* dl, const NetworkView& v, ImVec2 at, float room) {
    const float top = at.y;
    const float size = k.s(tok::kBody);
    const float title_h = k.s(tok::kS6);
    const auto title = [&](const char* text, ImVec4 ink) {
        label_at(k, dl, k.bold, k.s(tok::kTitle), {at.x, at.y, at.x + room, at.y + title_h},
                 LabelAlign::Left, ink, text);
        at.y += title_h + k.s(tok::kS2);
    };
    if (v.state == ShareState::Shared) {
        title("Shared as etude", col::kAccent);
        at.y += labelled_line(k, dl, at, room, "Windows", share_windows_address());
        at.y += labelled_line(k, dl, at, room, "Mac and Linux", share_smb_address());
        at.y += wrapped_text(k, dl, at, room, size, col::kText2,
                             "Sign in as etude with the password the setup gave. Songs you drop "
                             "in appear in the library.");
    } else if (v.state == ShareState::Checking) {
        title("Checking", col::kText);
    } else {
        title("Set up with the Pi", col::kText);
        at.y += wrapped_text(k, dl, at, room, size, col::kText2,
                             "On the Pi, in the etude folder, run this one command. It asks "
                             "for a password. Then songs you drop into the shared folder appear "
                             "in the library.") +
                k.s(tok::kS3);
        const float box_h = k.s(tok::kHit);
        const float inset = k.s(tok::kS3);
        dl->AddRectFilled({at.x, at.y}, {at.x + room, at.y + box_h}, u32(col::kRaised),
                          k.s(tok::kRadiusControl));
        label_at(k, dl, k.bold, size, {at.x + inset, at.y, at.x + room - inset, at.y + box_h},
                 LabelAlign::Left, col::kText,
                 fit(k.bold, size, kShareSetupCommand, room - 2.0f * inset).c_str());
        at.y += box_h;
    }
    return at.y - top;
}

} // namespace

void draw_network_sheet(Kit& k, SourcesPort& port, ImVec2 origin, float w, float body_h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const SubviewGeometry g = subview_geometry(w, body_h, k.u, false);
    if (back_chip(k, origin, g)) {
        port.set_open(SourcesSheet::None);
    }
    head_title(k, dl, origin, g, "Network folder", col::kText);
    const Box body = box_at(origin, g.body);
    const float pad = k.s(tok::kS4);
    // The words go on the front channel and the card behind them, as tall as they came out.
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    const float text_h = network_sheet_text(k, dl, port.network(), {body.x0 + pad, body.y0 + pad},
                                            body.w() - 2.0f * pad);
    dl->ChannelsSetCurrent(0);
    card(k, dl, {body.x0, body.y0}, {body.x1, std::min(body.y1, body.y0 + text_h + 2.0f * pad)},
         k.s(tok::kRadiusCard), col::kFill, ImDrawFlags_RoundCornersAll, 0.0f);
    dl->ChannelsMerge();
}

namespace {

// The words of the head while a copy runs or after one.
std::string usb_head_text(const UsbView& v) {
    if (v.importing) {
        return "Copying " + std::to_string(v.import_done + 1) + " of " +
               std::to_string(v.import_total);
    }
    if (v.ended == ImportEnd::Removed) {
        return "Stick removed · " + std::to_string(v.copied) + " copied";
    }
    if (v.ended == ImportEnd::Done) {
        std::string text = std::to_string(v.copied) + " copied";
        if (v.duplicates > 0) {
            text += " · " + std::to_string(v.duplicates) + " already there";
        }
        if (v.failed > 0) {
            text += " · " + std::to_string(v.failed) + " failed";
        }
        return text;
    }
    if (!v.present) {
        return "No stick";
    }
    return v.label + " · " + songs_text(v.songs) + (v.truncated ? "+" : "");
}

// What stands where a row's Import button was after the song was handled.
void draw_mark(Kit& k, ImDrawList* dl, const Box& slot, SongMark mark) {
    const char* icon_name = mark == SongMark::Failed ? icon::kWarning : icon::kCheck;
    const char* words = mark == SongMark::Copied      ? "Copied"
                        : mark == SongMark::Duplicate ? "Already there"
                                                      : "Failed";
    const ImVec4 ink = mark == SongMark::Failed ? col::kWarning : col::kText2;
    const float size = k.s(tok::kCaption);
    glyph(k, dl, icon_name, {slot.x0 + k.s(tok::kIconSm) * 0.5f, slot.cy()}, k.s(tok::kIconSm),
          ink);
    const float x = slot.x0 + k.s(tok::kIconSm) + k.s(tok::kS1);
    label_at(k, dl, k.regular, size, {x, slot.y0, slot.x1, slot.y1}, LabelAlign::Left, ink,
             fit(k.regular, size, words, slot.x1 - x).c_str());
}

void draw_usb_row(Kit& k, SourcesPort& port, const UsbView& v, std::size_t i, ImVec2 at, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const UsbRowGeometry r = usb_row_geometry(w, 0.0f, k.u);
    const UsbSong& song = v.list[i];
    dl->AddLine({at.x, at.y + r.step}, {at.x + w, at.y + r.step}, u32(col::kHairline),
                k.s(tok::kHairline));
    const Box name = box_at(at, r.name);
    label_at(k, dl, k.regular, k.s(tok::kBody), name, LabelAlign::Left, col::kText,
             fit(k.regular, k.s(tok::kBody), song.name, name.w()).c_str());
    const std::string size = size_text(song.size);
    label_at(k, dl, k.regular, k.s(tok::kCaption), box_at(at, r.size), LabelAlign::Right,
             col::kText2, size.c_str());
    const Box button = box_at(at, r.button);
    if (song.mark != SongMark::None) {
        draw_mark(k, dl, button, song.mark);
        return;
    }
    const std::string id = "##usb_import_" + std::to_string(i);
    ChipOpts o;
    o.icon = icon::kLoad;
    o.width = button.w();
    o.live = !v.importing;
    const float lift = (button.h() - k.s(tok::kHit)) * 0.5f;
    if (chip(k, id.c_str(), "Import", {button.x0, button.y0 + lift}, o)) {
        port.import_one(i);
    }
}

} // namespace

void draw_usb_list(Kit& k, SourcesPort& port, ImVec2 origin, float w, float body_h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const UsbView& v = port.usb();
    const SubviewGeometry g = subview_geometry(w, body_h, k.u, true);
    if (back_chip(k, origin, g)) {
        port.set_open(SourcesSheet::None);
    }
    head_title(k, dl, origin, g, usb_head_text(v),
               v.ended == ImportEnd::Removed ? col::kWarning : col::kText);
    ChipOpts action;
    action.width = g.action.w;
    action.live = v.present && v.songs > 0;
    const float lift = (g.action.h - k.s(tok::kHit)) * 0.5f;
    const ImVec2 action_at{origin.x + g.action.x, origin.y + g.action.y + lift};
    if (v.importing) {
        action.icon = icon::kClose;
        if (chip(k, "##usb_cancel", "Cancel", action_at, action)) {
            port.cancel_import();
        }
    } else {
        action.icon = icon::kLoad;
        if (chip(k, "##usb_import_all", "Import all", action_at, action)) {
            port.import_all();
        }
    }
    const Box body = box_at(origin, g.body);
    if (!v.present || v.list.empty()) {
        const char* text = !v.present   ? "The USB stick is not plugged in"
                           : v.scanning ? "Looking for songs"
                                        : "No songs on this stick";
        label_at(k, dl, k.regular, k.s(tok::kBody),
                 {body.x0, body.y0, body.x1, body.y0 + k.s(tok::kHit)}, LabelAlign::Left,
                 col::kText2, text);
        return;
    }
    ImGui::SetCursorScreenPos({body.x0, body.y0});
    ImGui::BeginChild("##usb-list", {body.w(), body.h()}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    const float step = usb_row_geometry(body.w(), 0.0f, k.u).step;
    for (std::size_t i = 0; i < v.list.size(); ++i) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        draw_usb_row(k, port, v, i, at, body.w());
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({body.w(), step});
    }
    if (v.truncated) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        label_at(k, ImGui::GetWindowDrawList(), k.regular, k.s(tok::kCaption),
                 {at.x, at.y, at.x + body.w(), at.y + step}, LabelAlign::Left, col::kText2,
                 "More songs than are listed here");
        ImGui::Dummy({body.w(), step});
    }
    drag_scroll(k);
    ImGui::EndChild();
}

} // namespace etude::app::ui
