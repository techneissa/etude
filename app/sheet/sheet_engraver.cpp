// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_engraver.hpp"

#include "jsonxx.h"
#include "lunasvg.h"
#include "toolkit.h"
#include "vrv.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace etude::app {

std::span<const unsigned char> embedded_font_regular();
std::span<const unsigned char> embedded_font();

namespace {

// Verovio draws a staff space of 18 SVG pixels at a scale of 100%.
constexpr float kVerovioSpaceAt100 = 18.0f;
// Room left of the staves for the brace, in Verovio's units at 100%.
constexpr int kBraceRoomUnits = 70;
// Transparent pixels round a system's box, for the antialiased edges.
constexpr float kPicturePad = 2.0f;

bool has_class(const lunasvg::Element& e, std::string_view cls) {
    const std::string& c = e.getAttribute("class");
    std::string_view v(c);
    std::size_t pos = 0;
    while (pos < v.size()) {
        std::size_t end = v.find(' ', pos);
        if (end == std::string_view::npos) {
            end = v.size();
        }
        if (v.substr(pos, end - pos) == cls) {
            return true;
        }
        pos = end + 1;
    }
    return false;
}

float attr_float(const lunasvg::Element& e, const char* name) {
    return std::strtof(e.getAttribute(name).c_str(), nullptr);
}

// The note ids ties end on, from the SVG text (`tie@endid`, written as data-endid="#id"; lunasvg
// keeps no attribute it does not know).
void read_tie_ends(const std::string& svg, std::unordered_set<std::string>& out) {
    static constexpr std::string_view kKey = "data-endid=\"#";
    for (std::size_t at = svg.find(kKey); at != std::string::npos; at = svg.find(kKey, at)) {
        at += kKey.size();
        const std::size_t end = svg.find('"', at);
        if (end == std::string::npos) {
            break;
        }
        out.emplace(svg, at, end - at);
    }
}

// Text in the score (tempo words, lyrics, fingering) is drawn with the app's own font, whatever
// family the SVG names: lunasvg loads no system fonts.
void register_fonts() {
    static std::once_flag once;
    std::call_once(once, [] {
        const std::span<const unsigned char> regular = embedded_font_regular();
        const std::span<const unsigned char> bold = embedded_font();
        for (const char* family : {"Times", "Times New Roman", "DejaVu Serif", "Arial",
                                   "DejaVu Sans", "Liberation Serif"}) {
            for (const bool italic : {false, true}) {
                lunasvg_add_font_face_from_data(family, false, italic, regular.data(),
                                                regular.size(), nullptr, nullptr);
                lunasvg_add_font_face_from_data(family, true, italic, bold.data(), bold.size(),
                                                nullptr, nullptr);
            }
        }
    });
}

struct Box {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

} // namespace

struct SheetEngraver::Impl {
    vrv::Toolkit toolkit{false};
    bool resources_ok = false;
    std::vector<std::string> svgs; // one page per system
    struct SystemRef {
        int page = 0;
        Box box; // the picture's rect in the page, pixels
        std::vector<std::string> mark_ids; // note and stem marks, hidden in the base picture
        std::vector<Box> heads;            // note heads, in the picture
    };
    std::vector<SystemRef> systems;
    float staff_space_px = 10.0f; // as engraved
};

SheetEngraver::SheetEngraver(std::string resource_dir) : impl_(std::make_unique<Impl>()) {
    vrv::EnableLog(false);
    register_fonts();
    impl_->resources_ok = impl_->toolkit.SetResourcePath(resource_dir);
}

SheetEngraver::~SheetEngraver() = default;

namespace {

struct TimeInfo {
    std::unordered_map<std::string, double> onset; // element id -> first quarter-note stamp
};

TimeInfo read_timemap(vrv::Toolkit& tk) {
    TimeInfo info;
    jsonxx::Array arr;
    if (!arr.parse(tk.RenderToTimemap("{\"includeMeasures\": true, \"includeRests\": true}"))) {
        return info;
    }
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (!arr.has<jsonxx::Object>(static_cast<unsigned>(i))) {
            continue;
        }
        const jsonxx::Object& o = arr.get<jsonxx::Object>(static_cast<unsigned>(i));
        if (!o.has<jsonxx::Number>("qstamp")) {
            continue;
        }
        const auto q = static_cast<double>(o.get<jsonxx::Number>("qstamp"));
        if (o.has<jsonxx::String>("measureOn")) {
            info.onset.emplace(o.get<jsonxx::String>("measureOn"), q);
        }
        for (const char* key : {"on", "restsOn"}) {
            if (!o.has<jsonxx::Array>(key)) {
                continue;
            }
            const jsonxx::Array& on = o.get<jsonxx::Array>(key);
            for (std::size_t k = 0; k < on.size(); ++k) {
                if (on.has<jsonxx::String>(static_cast<unsigned>(k))) {
                    info.onset.emplace(on.get<jsonxx::String>(static_cast<unsigned>(k)), q);
                }
            }
        }
    }
    return info;
}

// What one page's walk finds.
struct PageWalk {
    std::vector<SheetMark> marks;
    std::vector<std::string> mark_ids;
    std::vector<std::string> measure_of_mark;
    std::vector<std::string> chord_of_mark;
    std::vector<std::pair<std::string, Box>> measures; // in order
    std::vector<Box> obstacles;
    int staves = 0; // the most staves in one of its measures
    Box content;
    float staff_top = 1e9f;
    float staff_bottom = -1e9f;
    bool any = false;
};

class Walker {
  public:
    Walker(float k, float mx, float my, float space_px, PageWalk& out)
        : k_(k), mx_(mx), my_(my), space_px_(space_px), out_(out) {}

    void walk(const lunasvg::Element& e, const std::string& chord, const std::string& measure) {
        for (const lunasvg::Node& n : e.children()) {
            if (!n.isElement()) {
                continue;
            }
            const lunasvg::Element c = n.toElement();
            if (has_class(c, "bounding-box")) {
                take_box(c, e, chord, measure);
                continue;
            }
            if (has_class(c, "label") || has_class(c, "labelAbbr")) {
                continue;
            }
            const bool staff = has_class(c, "staff");
            if (staff) {
                staff_lines(c);
                staff_ = staff_in_measure_++;
                out_.staves = std::max(out_.staves, staff_in_measure_);
            }
            if (has_class(c, "ledgerLines")) {
                ledger_lines(c);
            }
            if (has_class(c, "measure")) {
                out_.measures.emplace_back(c.getAttribute("id"), Box{});
                staff_in_measure_ = 0;
            }
            const bool note = has_class(c, "note");
            const int outer_note = note_;
            if (note) {
                note_ = -1; // set when the note's own box is read, its first child
            }
            walk(c, has_class(c, "chord") ? c.getAttribute("id") : chord,
                 has_class(c, "measure") ? c.getAttribute("id") : measure);
            if (note) {
                note_ = outer_note;
            }
        }
    }

  private:
    Box rect_of(const lunasvg::Element& bbox_group) const {
        for (const lunasvg::Node& n : bbox_group.children()) {
            if (n.isElement()) {
                const lunasvg::Element r = n.toElement();
                return {(attr_float(r, "x") + mx_) * k_, (attr_float(r, "y") + my_) * k_,
                        attr_float(r, "width") * k_, attr_float(r, "height") * k_};
            }
        }
        return {};
    }

    // Ledger lines, as obstacles for the note names: paths "M x y L x2 y".
    void ledger_lines(const lunasvg::Element& group) {
        for (const lunasvg::Node& n : group.children()) {
            if (!n.isElement()) {
                continue;
            }
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (std::sscanf(n.toElement().getAttribute("d").c_str(), "M%f %f L%f %f", &x1, &y1, &x2, &y2) != 4) {
                continue;
            }
            const float half = 0.08f * space_px_;
            out_.obstacles.push_back({(std::min(x1, x2) + mx_) * k_, (y1 + my_) * k_ - half,
                                      std::fabs(x2 - x1) * k_, 2.0f * half});
        }
    }

    // The staff lines' extent: paths "M x y L x2 y" straight under the staff group.
    void staff_lines(const lunasvg::Element& staff) {
        for (const lunasvg::Node& n : staff.children()) {
            if (!n.isElement()) {
                continue;
            }
            const lunasvg::Element p = n.toElement();
            const std::string& d = p.getAttribute("d");
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (d.empty() || std::sscanf(d.c_str(), "M%f %f L%f %f", &x1, &y1, &x2, &y2) != 4 ||
                std::fabs(y1 - y2) > 0.5f) {
                continue;
            }
            const float y = (y1 + my_) * k_;
            out_.staff_top = std::min(out_.staff_top, y);
            out_.staff_bottom = std::max(out_.staff_bottom, y);
        }
    }

    void take_box(const lunasvg::Element& bbox, const lunasvg::Element& owner,
                  const std::string& chord, const std::string& measure) {
        const Box b = rect_of(bbox);
        if (!(b.w > 0.0f) || !(b.h > 0.0f)) {
            return;
        }
        const std::string& id_attr = bbox.getAttribute("id");
        const std::string id = id_attr.size() > 5 ? id_attr.substr(5) : std::string();
        // A bar's extent is its staves' (the bar's own box is empty).
        if (has_class(bbox, "staff") && !out_.measures.empty() && out_.measures.back().first == measure) {
            Box& mb = out_.measures.back().second;
            if (mb.w <= 0.0f) {
                mb = b;
            } else {
                const float x0 = std::min(mb.x, b.x);
                const float x1 = std::max(mb.x + mb.w, b.x + b.w);
                mb.x = x0;
                mb.w = x1 - x0;
            }
        }
        const bool note = has_class(bbox, "note");
        const bool rest = has_class(bbox, "rest") || has_class(bbox, "mRest");
        if (!structural(bbox)) {
            out_.obstacles.push_back(b);
        }
        const bool part = has_class(bbox, "stem") || has_class(bbox, "flag") ||
                          has_class(bbox, "dots") || has_class(bbox, "accid");
        if (part && note_ >= 0) {
            // A note's own stem, flag, dots and accidental change colour with it.
            SheetMark& nm = out_.marks[static_cast<std::size_t>(note_)];
            const float x0 = std::min(nm.box.x, b.x);
            const float y0 = std::min(nm.box.y, b.y);
            const float x1 = std::max(nm.box.x + nm.box.w, b.x + b.w);
            const float y1 = std::max(nm.box.y + nm.box.h, b.y + b.h);
            nm.box = {x0, y0, x1 - x0, y1 - y0};
            grow_content(b);
            return;
        }
        // A chord's stem, flag and dots follow the whole chord.
        const bool stem = !chord.empty() && part && !has_class(bbox, "accid");
        if (note || rest || stem) {
            SheetMark m;
            m.kind = note ? SheetMarkKind::Note : rest ? SheetMarkKind::Rest : SheetMarkKind::Stem;
            m.box = {b.x, b.y, b.w, b.h};
            m.head = m.box; // a note's own box is its head; its stem and flag come after
            m.head_x = b.x + b.w * 0.5f;
            m.staff = staff_;
            if (note) {
                // The cursor goes through the middle of the head: the head glyph's left edge (its
                // use transform) plus half its width, which depends on the glyph.
                for (const lunasvg::Node& n : owner.children()) {
                    if (!n.isElement() || !has_class(n.toElement(), "notehead")) {
                        continue;
                    }
                    for (const lunasvg::Node& hn : n.toElement().children()) {
                        if (!hn.isElement()) {
                            continue;
                        }
                        const lunasvg::Element use = hn.toElement();
                        float tx = 0.0f;
                        if (std::sscanf(use.getAttribute("transform").c_str(), "translate(%f", &tx) != 1) {
                            continue;
                        }
                        const std::string& href = use.getAttribute("xlink:href").empty()
                                                      ? use.getAttribute("href")
                                                      : use.getAttribute("xlink:href");
                        m.head_x = (tx + mx_) * k_ + head_half_width(href) * space_px_;
                    }
                }
            }
            if (note) {
                note_ = static_cast<int>(out_.marks.size());
            }
            out_.marks.push_back(m);
            out_.mark_ids.push_back(id);
            out_.measure_of_mark.push_back(measure);
            out_.chord_of_mark.push_back(stem || note ? chord : std::string());
        }
        grow_content(b);
    }

    // Boxes that hold others (a bar, a staff, a layer, a chord) or draw nothing: not obstacles.
    static bool structural(const lunasvg::Element& bbox) {
        for (const char* cls : {"system", "section", "measure", "staff", "layer", "chord", "mdiv",
                                "score", "space", "mNum", "pgHead", "pgFoot", "grpSym", "barLine"}) {
            if (has_class(bbox, cls)) {
                return true;
            }
        }
        return false;
    }

    // The picture holds everything that draws, text included.
    void grow_content(const Box& b) {
        if (!out_.any) {
            out_.content = b;
            out_.any = true;
            return;
        }
        const float x0 = std::min(out_.content.x, b.x);
        const float y0 = std::min(out_.content.y, b.y);
        const float x1 = std::max(out_.content.x + out_.content.w, b.x + b.w);
        const float y1 = std::max(out_.content.y + out_.content.h, b.y + b.h);
        out_.content = {x0, y0, x1 - x0, y1 - y0};
    }

    // Half the width of a note head glyph, in staff spaces (Bravura: the black and half heads are
    // 1.18 spaces wide, the whole note 1.69, the breve 2.0).
    static float head_half_width(const std::string& href) {
        if (href.find("E0A2") != std::string::npos) {
            return 0.845f;
        }
        if (href.find("E0A0") != std::string::npos || href.find("E0A1") != std::string::npos) {
            return 1.0f;
        }
        return 0.59f;
    }

    float k_;
    float mx_;
    float my_;
    float space_px_;
    PageWalk& out_;
    int note_ = -1; // the mark of the note being walked, -1 outside one
    int staff_ = 0;  // the staff being walked, 0 for the top one in its measure
    int staff_in_measure_ = 0;
};

// The scale from the SVG's view box to its pixels, and the page margin's offset (both read from
// the SVG text, as Verovio writes it).
void page_geometry(const std::string& svg, const lunasvg::Document& doc, float& k, float& mx,
                   float& my) {
    k = 1.0f;
    mx = 0.0f;
    my = 0.0f;
    const std::size_t inner = svg.find("<svg", 1);
    const std::size_t vb = svg.find("viewBox=\"", inner == std::string::npos ? 0 : inner);
    float v0 = 0, v1 = 0, vw = 0, vh = 0;
    if (vb != std::string::npos &&
        std::sscanf(svg.c_str() + vb + 9, "%f %f %f %f", &v0, &v1, &vw, &vh) == 4 && vw > 0.0f) {
        k = doc.width() / vw;
    }
    const std::size_t pm = svg.find("class=\"page-margin\"");
    if (pm != std::string::npos) {
        const std::size_t t = svg.find("translate(", pm);
        if (t != std::string::npos) {
            std::sscanf(svg.c_str() + t + 10, "%f, %f", &mx, &my);
        }
    }
}

} // namespace

bool SheetEngraver::engrave(const std::string& musicxml, const EngraveOptions& options,
                            SheetLayout& out, std::string& error) {
    Impl& im = *impl_;
    out = SheetLayout{};
    im.svgs.clear();
    im.systems.clear();
    if (!im.resources_ok) {
        error = "the music font was not found";
        return false;
    }
    const int scale = std::clamp(
        static_cast<int>(std::lround(options.staff_space_px / kVerovioSpaceAt100 * 100.0f)), 5, 1000);
    const int page_w = std::clamp(
        static_cast<int>(std::lround(options.width_px * 100.0f / static_cast<float>(scale))), 300, 60000);
    char json[768];
    std::snprintf(json, sizeof json,
                  "{\"pageWidth\": %d, \"pageHeight\": 100, \"scale\": %d, \"adjustPageHeight\": true,"
                  " \"header\": \"none\", \"footer\": \"none\", \"breaks\": \"auto\","
                  " \"pageMarginLeft\": %d, \"pageMarginRight\": 0, \"pageMarginTop\": 0,"
                  " \"pageMarginBottom\": 0, \"justifyVertically\": false, \"svgBoundingBoxes\": true,"
                  " \"svgRemoveXlink\": false, \"font\": \"Bravura\", \"xmlIdSeed\": 1,"
                  " \"mnumInterval\": 0, \"svgAdditionalAttribute\": [\"tie@endid\"],"
                  " \"spacingLinear\": %.3f, \"spacingNonLinear\": %.3f}",
                  page_w, scale, kBraceRoomUnits,
                  options.spacing_linear > 0.0f ? static_cast<double>(options.spacing_linear) : 0.25,
                  options.spacing_non_linear > 0.0f ? static_cast<double>(options.spacing_non_linear) : 0.6);
    im.toolkit.SetOptions(json);
    if (!im.toolkit.LoadData(musicxml)) {
        error = "the engraver could not read the score";
        return false;
    }
    const int pages = im.toolkit.GetPageCount();
    if (pages <= 0) {
        error = "the engraver laid out no page";
        return false;
    }
    im.svgs.reserve(static_cast<std::size_t>(pages));
    for (int p = 1; p <= pages; ++p) {
        im.svgs.push_back(im.toolkit.RenderToSVG(p, false));
        if (const char* dbg = std::getenv("ETUDE_SHEET_SVG_DIR")) {
            const std::string path = std::string(dbg) + "/page-" + std::to_string(p) + ".svg";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fwrite(im.svgs.back().data(), 1, im.svgs.back().size(), f);
                std::fclose(f);
            }
        }
    }
    const TimeInfo times = read_timemap(im.toolkit);
    im.toolkit.RenderToMIDI();

    const float space_px = kVerovioSpaceAt100 * static_cast<float>(scale) / 100.0f;
    im.staff_space_px = space_px;
    std::unordered_map<std::string, int> written_bar; // measure id -> written index
    std::unordered_map<std::string, int> chord_index;
    std::unordered_set<std::string> tie_ends; // a tie may end on the next system or page
    for (const std::string& svg : im.svgs) {
        read_tie_ends(svg, tie_ends);
    }
    for (int p = 0; p < pages; ++p) {
        const std::string& svg = im.svgs[static_cast<std::size_t>(p)];
        auto doc = lunasvg::Document::loadFromData(svg);
        if (!doc) {
            continue;
        }
        float k = 1.0f, mx = 0.0f, my = 0.0f;
        page_geometry(svg, *doc, k, mx, my);
        lunasvg::ElementList systems = doc->querySelectorAll("g.system");
        std::erase_if(systems, [](const lunasvg::Element& e) { return has_class(e, "bounding-box"); });
        for (const lunasvg::Element& sys : systems) {
            PageWalk walk;
            Walker(k, mx, my, space_px, walk).walk(sys, std::string(), std::string());
            if (!walk.any) {
                continue;
            }
            const int index = static_cast<int>(out.systems.size());
            Impl::SystemRef ref;
            ref.page = p;
            ref.box = {walk.content.x - kPicturePad, walk.content.y - kPicturePad,
                       walk.content.w + 2.0f * kPicturePad, walk.content.h + 2.0f * kPicturePad};
            const float ox = ref.box.x;
            const float oy = ref.box.y;
            SheetSystem s;
            s.width = std::ceil(ref.box.w);
            s.height = std::ceil(ref.box.h);
            s.staff_top = walk.staff_top < walk.staff_bottom ? walk.staff_top - oy : 0.0f;
            s.staff_bottom = walk.staff_top < walk.staff_bottom ? walk.staff_bottom - oy : s.height;
            s.staves = std::max(1, walk.staves);
            out.systems.push_back(s);
            for (const Box& b : walk.obstacles) {
                out.obstacles.push_back({index, {b.x - ox, b.y - oy, b.w, b.h}});
            }
            for (const auto& [id, b] : walk.measures) {
                const auto [it, added] = written_bar.emplace(id, static_cast<int>(written_bar.size()));
                (void)added;
                if (b.w > 0.0f) {
                    out.bars.push_back({it->second, index, b.x - ox, b.x + b.w - ox});
                }
            }
            for (std::size_t i = 0; i < walk.marks.size(); ++i) {
                SheetMark m = walk.marks[i];
                m.system = index;
                m.box.x -= ox;
                m.box.y -= oy;
                m.head_x -= ox;
                m.head.x -= ox;
                m.head.y -= oy;
                const std::string& id = walk.mark_ids[i];
                const auto bar = written_bar.find(walk.measure_of_mark[i]);
                m.bar = bar != written_bar.end() ? bar->second : -1;
                const auto on = times.onset.find(id);
                const auto bar_on = times.onset.find(walk.measure_of_mark[i]);
                m.offset_q = on != times.onset.end() && bar_on != times.onset.end()
                                 ? on->second - bar_on->second
                                 : 0.0;
                if (!walk.chord_of_mark[i].empty()) {
                    const auto [c, added] = chord_index.emplace(walk.chord_of_mark[i],
                                                                static_cast<int>(chord_index.size()));
                    (void)added;
                    m.chord = c->second;
                }
                if (m.kind == SheetMarkKind::Note) {
                    m.tied = tie_ends.contains(id);
                    jsonxx::Object o;
                    if (o.parse(im.toolkit.GetMIDIValuesForElement(id)) &&
                        o.has<jsonxx::Number>("pitch")) {
                        m.pitch = static_cast<int>(o.get<jsonxx::Number>("pitch"));
                    }
                }
                if (m.kind != SheetMarkKind::Rest) {
                    ref.mark_ids.push_back(id);
                }
                if (m.kind == SheetMarkKind::Note && !m.head.empty()) {
                    ref.heads.push_back({m.head.x, m.head.y, m.head.w, m.head.h});
                }
                out.marks.push_back(m);
            }
            im.systems.push_back(std::move(ref));
        }
    }
    if (out.systems.empty()) {
        error = "the engraver drew no system";
        return false;
    }
    return true;
}

namespace {

// Hides every branch of the tree that holds none of `keep`; true when `e` holds one.
bool keep_only(lunasvg::Element e, const std::vector<std::string>& keep) {
    const std::string& id = e.getAttribute("id");
    if (!id.empty() && std::binary_search(keep.begin(), keep.end(), id)) {
        return true;
    }
    bool any = false;
    for (const lunasvg::Node& n : e.children()) {
        if (n.isElement()) {
            any = keep_only(n.toElement(), keep) || any;
        }
    }
    if (!any && !has_class(e, "definition-scale")) {
        e.setAttribute("display", "none");
    }
    return any;
}

void to_rgba(lunasvg::Bitmap& bitmap, std::vector<std::uint8_t>& out) {
    bitmap.convertToRGBA();
    const int w = bitmap.width();
    const int h = bitmap.height();
    out.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        std::memcpy(out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4,
                    bitmap.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.stride()),
                    static_cast<std::size_t>(w) * 4);
    }
}

// The ring round each head: the head's own shape from the notes mask with its hollow filled
// (transparent pixels the head's edge encloses), grown by `r` pixels; what lies outside that shape
// is the ring. Coverage, antialiased at the ring's outer edge; a ring shared by two heads keeps the
// stronger.
void draw_rings(const std::vector<std::uint8_t>& notes, int w, int h, const std::vector<Box>& heads,
                float r, std::vector<std::uint8_t>& rings) {
    rings.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    const auto alpha = [&](int x, int y) {
        return notes[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4 + 3];
    };
    const int reach = static_cast<int>(std::ceil(r)) + 1;
    std::vector<std::uint8_t> shape;
    std::vector<int> queue;
    for (const Box& b : heads) {
        const int x0 = std::max(0, static_cast<int>(std::floor(b.x)) - 1);
        const int y0 = std::max(0, static_cast<int>(std::floor(b.y)) - 1);
        const int x1 = std::min(w, static_cast<int>(std::ceil(b.x + b.w)) + 1);
        const int y1 = std::min(h, static_cast<int>(std::ceil(b.y + b.h)) + 1);
        const int bw = x1 - x0;
        const int bh = y1 - y0;
        if (bw <= 0 || bh <= 0) {
            continue;
        }
        // The head's shape: its pixels, and the transparent ones no path of transparent pixels
        // joins to the box's edge (the hollow of a half or whole note).
        shape.assign(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh), 0);
        constexpr std::uint8_t kOpen = 1; // transparent and joined to the edge
        std::vector<std::uint8_t> open(shape.size(), 0);
        queue.clear();
        const auto try_open = [&](int x, int y) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x);
            if (open[i] == 0 && alpha(x0 + x, y0 + y) < 128) {
                open[i] = kOpen;
                queue.push_back(static_cast<int>(i));
            }
        };
        for (int x = 0; x < bw; ++x) {
            try_open(x, 0);
            try_open(x, bh - 1);
        }
        for (int y = 0; y < bh; ++y) {
            try_open(0, y);
            try_open(bw - 1, y);
        }
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const int x = queue[q] % bw;
            const int y = queue[q] / bw;
            if (x > 0) try_open(x - 1, y);
            if (x + 1 < bw) try_open(x + 1, y);
            if (y > 0) try_open(x, y - 1);
            if (y + 1 < bh) try_open(x, y + 1);
        }
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x);
                shape[i] = open[i] != 0 ? alpha(x0 + x, y0 + y) : 255;
            }
        }
        // The ring: within `r` of the shape, outside it.
        const auto shape_at = [&](int x, int y) -> float {
            if (x < x0 || y < y0 || x >= x1 || y >= y1) {
                return 0.0f;
            }
            return static_cast<float>(shape[static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(bw) +
                                            static_cast<std::size_t>(x - x0)]) / 255.0f;
        };
        for (int y = std::max(0, y0 - reach); y < std::min(h, y1 + reach); ++y) {
            for (int x = std::max(0, x0 - reach); x < std::min(w, x1 + reach); ++x) {
                const float inside = shape_at(x, y);
                if (inside >= 1.0f) {
                    continue;
                }
                float near = 0.0f;
                for (int dy = -reach; dy <= reach && near < 1.0f; ++dy) {
                    for (int dx = -reach; dx <= reach; ++dx) {
                        const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                        const float weight = std::clamp(r + 0.5f - d, 0.0f, 1.0f);
                        if (weight > 0.0f) {
                            near = std::max(near, shape_at(x + dx, y + dy) * weight);
                        }
                    }
                }
                const float cover = near * (1.0f - inside);
                const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
                const auto a = static_cast<std::uint8_t>(std::lround(std::clamp(cover, 0.0f, 1.0f) * 255.0f));
                if (a > rings[o + 3]) {
                    rings[o] = rings[o + 1] = rings[o + 2] = 255;
                    rings[o + 3] = a;
                }
            }
        }
    }
}

} // namespace

float sheet_ring_px(float staff_space_px) { return std::max(1.5f, 0.16f * staff_space_px); }

bool SheetEngraver::draw_system(int system, SystemPicture& out) {
    Impl& im = *impl_;
    if (system < 0 || static_cast<std::size_t>(system) >= im.systems.size()) {
        return false;
    }
    const Impl::SystemRef& ref = im.systems[static_cast<std::size_t>(system)];
    const std::string& svg = im.svgs[static_cast<std::size_t>(ref.page)];
    const int w = static_cast<int>(std::ceil(ref.box.w));
    const int h = static_cast<int>(std::ceil(ref.box.h));
    if (w <= 0 || h <= 0) {
        return false;
    }
    std::vector<std::string> keep = ref.mark_ids;
    std::sort(keep.begin(), keep.end());
    const char* hide = "g.mNum, g.pgHead, g.pgFoot, g.bounding-box, g.content-bounding-box "
                       "{display:none}";
    const lunasvg::Matrix at = lunasvg::Matrix::translated(-ref.box.x, -ref.box.y);

    auto base = lunasvg::Document::loadFromData(svg);
    auto notes = lunasvg::Document::loadFromData(svg);
    if (!base || !notes) {
        return false;
    }
    base->applyStyleSheet(hide);
    notes->applyStyleSheet(hide);
    for (const std::string& id : ref.mark_ids) {
        lunasvg::Element e = base->getElementById(id);
        if (!e.isNull()) {
            e.setAttribute("display", "none");
        }
    }
    keep_only(notes->documentElement(), keep);
    for (lunasvg::Element& s : notes->querySelectorAll("g.system")) {
        s.setAttribute("color", "#ffffff");
        s.setAttribute("fill", "#ffffff");
        s.setAttribute("stroke", "#ffffff");
    }
    lunasvg::Bitmap a(w, h);
    a.clear(0x00000000);
    base->render(a, at);
    lunasvg::Bitmap b(w, h);
    b.clear(0x00000000);
    notes->render(b, at);
    out.width = w;
    out.height = h;
    to_rgba(a, out.base);
    to_rgba(b, out.notes);
    // Head boxes are in the picture already (engrave() moved them there).
    draw_rings(out.notes, w, h, ref.heads, sheet_ring_px(im.staff_space_px), out.rings);
    return true;
}

} // namespace etude::app
