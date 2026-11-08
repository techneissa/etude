// SPDX-License-Identifier: GPL-3.0-or-later
// The "Add songs" page: the Phone row with its switch, and under it, while the server is on, the QR
// code, the address, the PIN, what it is doing and the time left; then the network folder and the
// USB stick as two tiles, each opening a view of its own over the page (add_songs_sources.hpp). The
// page only reads the UploadPort's and SourcesPort's views and sets their switches and actions: the
// server and the readers run on threads of their own (app/upload_controller.hpp,
// app/sources_controller.hpp).
#include "pages.hpp"

#include "add_songs_sources.hpp"
#include "icons.hpp"
#include "qr_draw.hpp"
#include "upload_port.hpp"

#include "item_probe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace etude::app::ui {

namespace {

// The most lines the page's hint takes.
constexpr int kHintLines = 3;

// Seconds as "9:42".
void clock_text(char* out, std::size_t size, int seconds) {
    seconds = std::max(seconds, 0);
    std::snprintf(out, size, "%d:%02d", seconds / 60, seconds % 60);
}

Box box_at(ImVec2 origin, const Rect& r) {
    return {origin.x + r.x, origin.y + r.y, origin.x + r.right(), origin.y + r.bottom()};
}

// "PIN" and its four digits, in the boxes the layout gave them.
void draw_pin(Kit& k, ImDrawList* dl, ImVec2 origin, const InfoGeometry& in,
              const std::string& pin) {
    label_at(k, dl, k.regular, k.s(tok::kBody), box_at(origin, in.pin_label), LabelAlign::Left,
             col::kText2, "PIN");
    for (std::size_t i = 0; i < 4; ++i) {
        const Box box = box_at(origin, in.pin[i]);
        dl->AddRectFilled({box.x0, box.y0}, {box.x1, box.y1}, u32(col::kFill),
                          k.s(tok::kRadiusControl));
        dl->AddRect({box.x0, box.y0}, {box.x1, box.y1}, u32(col::kHairline),
                    k.s(tok::kRadiusControl), 0, k.s(tok::kHairline));
        const char digit[2] = {i < pin.size() ? pin[i] : '-', '\0'};
        label_at(k, dl, k.bold, k.s(tok::kDisplay), box, LabelAlign::Centre, col::kText, digit);
    }
}

// The file coming in: its name and percent over a bar, then how far along the visit is.
void draw_receiving(Kit& k, ImDrawList* dl, const Box& slot, const UploadView& v) {
    const float line = k.s(tok::kS5);
    const float size = k.s(tok::kBody);
    char percent[16];
    std::snprintf(percent, sizeof percent, "%d%%", v.percent);
    const float percent_w = text_size(k.regular, size, percent).x + k.s(tok::kS2);
    const std::string head = "Receiving " + v.file;
    const FitText shown = fit(k.regular, size, head, slot.w() - percent_w);
    label_at(k, dl, k.regular, size, {slot.x0, slot.y0, slot.x1, slot.y0 + line}, LabelAlign::Left,
             col::kText, shown.c_str());
    label_at(k, dl, k.regular, size, {slot.x0, slot.y0, slot.x1, slot.y0 + line}, LabelAlign::Right,
             col::kText, percent);
    const float bar_y = slot.y0 + line + k.s(tok::kS1);
    const float bar_h = k.s(phone_tok::kBarH);
    dl->AddRectFilled({slot.x0, bar_y}, {slot.x1, bar_y + bar_h}, u32(col::kFill), bar_h);
    const float fill = std::clamp(static_cast<float>(v.percent) / 100.0f, 0.0f, 1.0f);
    dl->AddRectFilled({slot.x0, bar_y}, {slot.x0 + slot.w() * fill, bar_y + bar_h},
                      u32(col::kAccent), bar_h);
    char left[16];
    clock_text(left, sizeof left, v.remaining_s);
    char status[64];
    std::snprintf(status, sizeof status, "%d received \u00b7 %s left", v.received, left);
    const float low = bar_y + bar_h + k.s(tok::kS1);
    label_at(k, dl, k.regular, size, {slot.x0, low, slot.x1, low + line}, LabelAlign::Left,
             col::kText2, status);
}

// The card: the code on the left, the address, the PIN and the state on the right; under each
// other when the page is too narrow for both.
void phone_card(Kit& k, ImVec2 origin, const AddSongsGeometry& g, const UploadView& v) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box card_box = box_at(origin, g.card);
    card(k, dl, {card_box.x0, card_box.y0}, {card_box.x1, card_box.y1}, k.s(tok::kRadiusCard),
         col::kFill, ImDrawFlags_RoundCornersAll, 0.0f);
    const Box qr = box_at(origin, g.qr);
    draw_qr(k, dl, {qr.x0, qr.y0}, qr.w(), qr_for(v.url));
    const InfoGeometry& in = g.info;
    label_at(k, dl, k.bold, in.name_size, box_at(origin, in.name), LabelAlign::Left, col::kText,
             fit(k.bold, in.name_size, v.name_address, in.name.w).c_str());
    const std::string number = "or " + v.number_address;
    label_at(k, dl, k.regular, k.s(tok::kBody), box_at(origin, in.number), LabelAlign::Left,
             col::kText2, fit(k.regular, k.s(tok::kBody), number, in.number.w).c_str());
    draw_pin(k, dl, origin, in, v.pin);
    const Box status = box_at(origin, in.status);
    if (v.receiving) {
        draw_receiving(k, dl, status, v);
        return;
    }
    const ChipOpts ready{icon::kWifi, nullptr, true, false};
    chip(k, "##phone_ready", "Ready", {status.x0, status.y0}, ready);
    char left[16];
    clock_text(left, sizeof left, v.remaining_s);
    const std::string text = std::string("Off in ") + left;
    const float next = status.x0 + chip_width(k, icon::kWifi, "Ready") + k.s(tok::kS2);
    const ChipOpts timer{icon::kClock, nullptr, false, false};
    chip(k, "##phone_left", text, {next, status.y0}, timer);
}

} // namespace

namespace {

// The first frame of a visit to the page: Samba and the stick are read again (on their threads),
// and a view left open on the last visit is closed. The very first visit keeps what a developer
// flag opened.
void on_page_visit(SourcesPort& sources) {
    static int last_frame = -10;
    static bool visited = false;
    const int frame = ImGui::GetFrameCount();
    if (frame - last_frame > 1) {
        sources.recheck();
        if (visited) {
            sources.set_open(SourcesSheet::None);
        }
        visited = true;
    }
    last_frame = frame;
}

// The two tiles under the card or the hint.
void source_tiles(Kit& k, SourcesPort& sources, ImVec2 origin, const AddSongsGeometry& g) {
    // The tiles' boxes, for the settings check to tap.
    for (std::size_t i = 0; i < 2; ++i) {
        const Box tile = box_at(origin, g.tiles[i]);
        probe::add_rect(i == 0 ? "##network-tile" : "##usb-tile", {tile.x0, tile.y0},
                        {tile.x1, tile.y1});
    }
    const NetworkView& net = sources.network();
    const float detail_room = g.tiles[0].w - 3.0f * k.s(tok::kS3) - k.s(tok::kIconLg);
    const std::string net_detail = network_tile_detail(k, net, detail_room);
    if (detail_tile(k, "##network", icon::kDrive, "Network folder", net_detail.c_str(), origin,
                    g.tiles[0], net.state != ShareState::Shared)) {
        sources.set_open(SourcesSheet::Network);
    }
    const UsbView& usb = sources.usb();
    const std::string usb_detail = usb_tile_detail(usb);
    if (detail_tile(k, "##usb", icon::kUsb, "USB stick", usb_detail.c_str(), origin, g.tiles[1],
                    !usb.present)) {
        if (usb.present && usb.songs > 0) {
            sources.set_open(SourcesSheet::Usb);
        } else {
            k.say(!usb.present   ? "Plug in a USB stick with songs on it"
                  : usb.scanning ? "Looking at the stick"
                                 : "There are no songs on this stick");
        }
    }
}

} // namespace

void page_add_songs(Kit& k, UiModel& model, int columns) {
    (void)model;
    (void)columns;
    static NullUploadPort none;
    static NullSourcesPort no_sources;
    UploadPort& port = k.upload != nullptr ? *k.upload : none;
    SourcesPort& sources = k.sources != nullptr ? *k.sources : no_sources;
    on_page_visit(sources);
    if (sources.open() != SourcesSheet::None) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetContentRegionAvail().y;
        if (sources.open() == SourcesSheet::Network) {
            draw_network_sheet(k, sources, start, width, height);
        } else {
            draw_usb_list(k, sources, start, width, height);
        }
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy({width, height});
        return;
    }
    const UploadView& v = port.view();
    // What the Phone row says under its name.
    char detail[64];
    ImVec4 ink = col::kAccent;
    if (!v.on) {
        std::snprintf(detail, sizeof detail, "%s", v.note.empty() ? "Off" : v.note.c_str());
        ink = v.note.empty() ? col::kText2 : col::kWarning;
    } else if (!v.ready) {
        std::snprintf(detail, sizeof detail, "Starting");
    } else if (v.url.empty()) {
        std::snprintf(detail, sizeof detail, "Not on a network yet");
        ink = col::kWarning;
    } else if (v.receiving) {
        std::snprintf(detail, sizeof detail, "Receiving");
    } else if (v.received > 0) {
        std::snprintf(detail, sizeof detail, "Last: %d song%s added", v.received,
                      v.received == 1 ? "" : "s");
    } else {
        std::snprintf(detail, sizeof detail, "Ready");
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool with_card = v.on && v.ready && !v.url.empty();
    // Without the card the page says in a line or two what to do; the tiles come after it.
    const char* hint =
        !v.on ? "Turn it on, scan the code with your phone's camera and type the PIN. "
                "It turns itself off after 10 minutes."
        : v.ready && !with_card
            ? "Connect the Pi to your home network, with Wi-Fi or a cable, then turn "
              "Phone off and on again."
            : "";
    const float body_h = ImGui::GetContentRegionAvail().y;
    const float hint_h = *hint != '\0' ? help_text_height(k, hint, w, kHintLines) : 0.0f;
    const AddSongsGeometry g = add_songs_geometry(w, body_h, k.u, with_card,
                                                  hint_h > 0.0f ? hint_h + k.s(tok::kS2) : 0.0f);
    bool on = v.on;
    if (switch_row_detail(k, icon::kPhone, "Phone", detail, ink, &on, g.head)) {
        port.set_on(on);
    }
    if (with_card) {
        phone_card(k, origin, g, v);
    } else if (hint_h > 0.0f) {
        ImGui::SetCursorScreenPos({origin.x, origin.y + g.head.row.h + k.s(tok::kS2)});
        help_text(k, hint, col::kText2, kHintLines);
    }
    source_tiles(k, sources, origin, g);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({w, g.height});
}

} // namespace etude::app::ui
