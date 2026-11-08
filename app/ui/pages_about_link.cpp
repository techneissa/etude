// SPDX-License-Identifier: GPL-3.0-or-later
// The sheet that replaces the About page's body when a link is tapped on a machine with no browser
// (the Pi): its name, a QR code of the address for a phone's camera, and the address in text. Laid
// out by about_layout.hpp.
#include "pages.hpp"

#include "about_layout.hpp"
#include "about_port.hpp"
#include "icons.hpp"
#include "qr_draw.hpp"

#include "../update_status.hpp"

#include <string>

namespace etude::app::ui {

namespace {

Box box_at(ImVec2 origin, const Rect& r) {
    return {origin.x + r.x, origin.y + r.y, origin.x + r.right(), origin.y + r.bottom()};
}

// The sheet's words for an address, built when the address changes and not in every frame.
struct SheetWords {
    std::string url;
    std::string name;
    std::string typed;
};

const SheetWords& sheet_words(const std::string& url) {
    static SheetWords words;
    if (words.url != url || words.name.empty()) {
        words.url = url;
        words.name = link_title(url);
        words.typed = link_address(url);
    }
    return words;
}

} // namespace

void draw_link_sheet(Kit& k, AboutPort& port, ImVec2 origin, float w, float body_h) {
    const SheetWords& words = sheet_words(port.link_sheet());
    const AboutSheet g = about_sheet(w, body_h, k.u);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float lift = (g.head.back.h - k.s(tok::kHit)) * 0.5f;
    ChipOpts back;
    back.icon = icon::kBack;
    back.width = g.head.back.w;
    if (chip(k, "##about_back", "Back", {origin.x + g.head.back.x, origin.y + g.head.back.y + lift},
             back)) {
        port.set_link_sheet("");
    }
    const Box title = box_at(origin, g.head.title);
    label_at(k, dl, k.bold, k.s(tok::kTitle), title, LabelAlign::Left, col::kText,
             fit(k.bold, k.s(tok::kTitle), words.name, title.w()).c_str());

    const Box qr = box_at(origin, g.qr);
    draw_qr(k, dl, {qr.x0, qr.y0}, qr.w(), qr_for(words.url));

    const LabelAlign align = g.side_by_side ? LabelAlign::Left : LabelAlign::Centre;
    const Box address = box_at(origin, g.address);
    label_at(k, dl, k.bold, k.s(tok::kTitle), address, align, col::kText,
             fit(k.bold, k.s(tok::kTitle), words.typed, address.w()).c_str());
    const Box hint = box_at(origin, g.hint);
    label_at(k, dl, k.regular, k.s(tok::kBody), hint, align, col::kText2,
             fit(k.regular, k.s(tok::kBody), "QR Code", hint.w()).c_str());
}

} // namespace etude::app::ui
