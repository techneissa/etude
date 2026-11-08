// SPDX-License-Identifier: GPL-3.0-or-later
// The audit's report lines for icons, hit areas, chips and controls with no face (icon_audit.hpp).
#include "icon_audit.hpp"

#include "hit_check.hpp"
#include "icon_audit_internal.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace etude::app {

// One `centred` or `play` line per icon button of the settled frame: where its ink sits against the
// button's body (`a` the frame with the icons, `b` without them, `fb` pixels per point).
void IconAudit::write_icons(std::FILE* out, const ui::PixelView& a, const ui::PixelView& b,
                            float fb) const {
    for (const Seen& s : settled_) {
        if (s.kind == ui::InkKind::Target) {
            continue;
        }
        const float x0 = s.min.x * fb;
        const float y0 = s.min.y * fb;
        const float x1 = s.max.x * fb;
        const float y1 = s.max.y * fb;
        const bool whole = x0 >= 0.0f && y0 >= 0.0f && x1 <= static_cast<float>(a.width) &&
                           y1 <= static_cast<float>(a.height);
        // The body and a little around it: the ink may reach past a tight pill.
        const ui::PixelRect area{static_cast<int>(std::floor(x0)) - 2,
                                 static_cast<int>(std::floor(y0)) - 2,
                                 static_cast<int>(std::ceil(x1)) + 2,
                                 static_cast<int>(std::ceil(y1)) + 2};
        const ui::InkBox ink = ui::measure_ink(a, b, area);
        const float cx = (x0 + x1) * 0.5f;
        const float cy = (y0 + y1) * 0.5f;
        const ui::Offset off = ui::judge_ink(ink, cx, cy, s.kind, s.check_y);
        const char* verdict = !whole ? "clipped" : !ink.found ? "none" : off.ok ? "ok" : "FAIL";
        std::fprintf(out, "%s\t%s\t%s\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%s\n",
                     label_.c_str(), s.name.c_str(),
                     s.kind == ui::InkKind::PlayTriangle ? "play" : "centred",
                     static_cast<double>(cx), static_cast<double>(cy),
                     static_cast<double>(ink.cx()), static_cast<double>(ink.cy()),
                     static_cast<double>(off.dx), static_cast<double>(off.dy),
                     static_cast<double>(ink.width()), static_cast<double>(ink.height()),
                     verdict);
    }
}

void IconAudit::write_hits(std::FILE* out) const {
    const float ppm = ui::audit::pt_per_mm();
    std::vector<ui::HitItem> items;
    items.reserve(settled_hits_.size());
    for (const ui::audit::Hit& h : settled_hits_) {
        ui::HitItem i;
        i.name = h.name;
        i.box = {h.min.x, h.min.y, h.max.x - h.min.x, h.max.y - h.min.y};
        const float x0 = std::max(h.min.x, h.clip_min.x);
        const float y0 = std::max(h.min.y, h.clip_min.y);
        const float x1 = std::min(h.max.x, h.clip_max.x);
        const float y1 = std::min(h.max.y, h.clip_max.y);
        i.visible = x1 > x0 && y1 > y0 ? RectF{x0, y0, x1 - x0, y1 - y0} : RectF{};
        i.cut = h.cut;
        i.surface = is_surface(h.name);
        i.covered = h.covered;
        items.push_back(std::move(i));
    }
    const ui::HitReport report = ui::check_hit_areas(items, ppm);
    const auto small = [&](const std::string& name) {
        for (const ui::HitFinding& f : report.findings) {
            if (f.fault == ui::HitFault::TooSmall && f.a == name) {
                return true;
            }
        }
        return false;
    };
    // One line per item: its size in millimetres. FAIL under 12 mm either way.
    for (const ui::audit::Hit& h : settled_hits_) {
        const float w = (h.max.x - h.min.x) / std::max(ppm, 0.01f);
        const float ht = (h.max.y - h.min.y) / std::max(ppm, 0.01f);
        const char* verdict = h.cut ? "cut" : small(h.name) ? "FAIL" : "ok";
        std::fprintf(out, "%s\t%s\thit\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t0\t0\t%s\n",
                     label_.c_str(), h.name.c_str(), static_cast<double>(h.min.x),
                     static_cast<double>(h.min.y), static_cast<double>(h.max.x),
                     static_cast<double>(h.max.y), static_cast<double>(w),
                     static_cast<double>(ht), verdict);
    }
    // The pairs that break a rule: the two names, where, and the overlap or the gap in mm.
    for (const ui::HitFinding& f : report.findings) {
        if (f.fault == ui::HitFault::TooSmall) {
            continue;
        }
        std::fprintf(out, "%s\t%s | %s\thit-pair\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t0\t0\t0\t%s\n",
                     label_.c_str(), f.a.c_str(), f.b.c_str(), static_cast<double>(f.where.x),
                     static_cast<double>(f.where.y), static_cast<double>(f.where.right()),
                     static_cast<double>(f.where.bottom()), static_cast<double>(f.mm),
                     ui::hit_fault_name(f.fault));
    }
    // That the rule was run: how many pairs were looked at, and how many items were covered.
    int covered = 0;
    for (const ui::audit::Hit& h : settled_hits_) {
        covered += h.covered ? 1 : 0;
    }
    std::fprintf(out, "%s\t(pairs)\thit-pairs\t0\t0\t0\t0\t%d\t%d\t%.3f\t0\t%s\n",
                 label_.c_str(), report.pairs, covered, static_cast<double>(ppm),
                 ppm > 0.0f ? "ok" : "none");
}

void IconAudit::write_labels(std::FILE* out, const ui::PixelView& a, const ui::PixelView& b,
                             float fb) const {
    // The labels: the baseline read from the pixels, the capitals' band standing on
    // it and the ink box, against the slot the label was given.
    std::vector<ui::FaceBox> face_boxes;
    face_boxes.reserve(settled_faces_.size());
    for (const ui::audit::Face& f : settled_faces_) {
        face_boxes.push_back({f.min.x, f.min.y, f.max.x, f.max.y, f.dl});
    }
    for (const ui::audit::Label& l : settled_labels_) {
        // Another label's capitals over this one's (a pill laid over a ruler's
        // numbers): the difference is both, so neither is judged. A label its
        // window's clip leaves out draws nothing to be over it.
        bool overlapped = false;
        for (const ui::audit::Label& o : settled_labels_) {
            overlapped = overlapped ||
                         (&o != &l && !o.out && l.band_min.x < o.band_max.x &&
                          o.band_min.x < l.band_max.x && l.band_min.y < o.band_max.y &&
                          o.band_min.y < l.band_max.y);
        }
        // Whether the capitals' band (not the line box, whose air and descenders may
        // hang off the screen) lies on the frame: fully off it, or partly cut.
        const float x0 = l.band_min.x * fb;
        const float y0 = l.band_min.y * fb;
        const float x1 = l.band_max.x * fb;
        const float y1 = l.band_max.y * fb;
        const float fw = static_cast<float>(a.width);
        const float fh = static_cast<float>(a.height);
        const bool off_screen = l.out || x1 <= 0.0f || y1 <= 0.0f || x0 >= fw || y0 >= fh;
        const bool whole = x0 >= 0.0f && y0 >= 0.0f && x1 <= fw && y1 <= fh;
        const ui::PixelRect area{
            static_cast<int>(std::floor(l.area_min.x * fb)) - 1,
            static_cast<int>(std::floor(l.area_min.y * fb)) - 1,
            static_cast<int>(std::ceil(l.area_max.x * fb)) + 1,
            static_cast<int>(std::ceil(l.area_max.y * fb)) + 1};
        const ui::LabelInk ink = ui::measure_label(a, b, area, l.descenders);
        const float cx = (l.slot_min.x + l.slot_max.x) * 0.5f * fb;
        const float cy = (l.slot_min.y + l.slot_max.y) * 0.5f * fb;
        const ui::LabelOffset off = ui::judge_label(
            ink, l.cap_h * fb, cx, cy, l.align == ui::LabelAlign::Centre, l.standing);
        // The control it is drawn on (`label_face`): the smallest face of its own list that holds
        // the middle or an end of its ink (not its slot: a left label's slot is the whole row).
        // The ink must stay inside that face, the part a person sees; a label on no face is
        // judged by its window's clip alone ("cut" above) and counted. The slot is not the
        // measure: it is sized from the text's advance, a pixel or two off the ink.
        const int on = ui::label_face(face_boxes, l.dl, ink.box.left / fb, ink.box.right / fb,
                                      ink.box.cy() / fb);
        // -1 in the TSV's face columns: on no face.
        const ui::FaceBox* face = on >= 0 ? &face_boxes[static_cast<std::size_t>(on)] : nullptr;
        const float face_l = face != nullptr ? face->left * fb : -1.0f;
        const float face_r = face != nullptr ? face->right * fb : -1.0f;
        const bool inside =
            face == nullptr || ui::label_inside(ink.box.left, ink.box.right, face_l, face_r);
        const char* verdict = off_screen         ? "offscreen"
                              : !whole || l.cut  ? "cut"
                              : overlapped       ? "overlap"
                              : !ink.found       ? "none"
                              : !inside          ? "spill"
                              : off.ok           ? "ok"
                                                 : "FAIL";
        std::fprintf(
            out,
            "%s\t%s\tlabel\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%s\t%.2f\t%.2f\t%.2f\t%.2f\n",
            label_.c_str(), l.text.c_str(), static_cast<double>(cx),
            static_cast<double>(cy), static_cast<double>(ink.box.cx()),
            static_cast<double>(ink.baseline - l.cap_h * fb * 0.5f),
            static_cast<double>(off.dx), static_cast<double>(off.dy),
            static_cast<double>(ink.box.width()), static_cast<double>(ink.box.height()), verdict,
            static_cast<double>(ink.box.left), static_cast<double>(ink.box.right),
            static_cast<double>(face_l), static_cast<double>(face_r));
    }
}

void IconAudit::write_chips(std::FILE* out) const {
    const float u = std::max(0.01f, ui::audit::unit());
    for (const ui::audit::Chip& c : settled_chips_) {
        const float w = (c.max.x - c.min.x) / u;
        const float h = (c.max.y - c.min.y) / u;
        const float cy = (c.min.y + c.max.y) * 0.5f;
        // The row's chip height: the tallest face among the chips on this line.
        float row_h = h;
        for (const ui::audit::Chip& o : settled_chips_) {
            if (std::fabs((o.min.y + o.max.y) * 0.5f - cy) <= ui::tok::kHit * u * 0.5f) {
                row_h = std::max(row_h, (o.max.y - o.min.y) / u);
            }
        }
        const float least_h = c.bar ? 0.0f : ui::tok::kFace;
        const float least_w = c.words ? ui::tok::kChipMinW : 0.0f;
        const bool ok =
            h >= row_h - kPointSlack && h >= least_h - kPointSlack && w >= least_w - kPointSlack;
        std::fprintf(out, "%s\t%s\tchip\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%s\n",
                     label_.c_str(), c.name.c_str(), static_cast<double>(c.min.x),
                     static_cast<double>(c.min.y), static_cast<double>(c.max.x),
                     static_cast<double>(c.max.y), static_cast<double>(w), static_cast<double>(h),
                     static_cast<double>(least_w), static_cast<double>(std::max(row_h, least_h)),
                     ok ? "ok" : "FAIL");
    }
}

void IconAudit::write_faceless(std::FILE* out) const {
    const auto inside = [](ImVec2 a_min, ImVec2 a_max, ImVec2 b_min, ImVec2 b_max) {
        return a_min.x >= b_min.x - kPointSlack && a_min.y >= b_min.y - kPointSlack &&
               a_max.x <= b_max.x + kPointSlack && a_max.y <= b_max.y + kPointSlack;
    };
    for (const ui::audit::Hit& h : forced_hits_) {
        if (is_surface(h.name)) {
            continue;
        }
        // A face belongs to the item when it lies whole inside its hit box, or the whole hit box
        // lies inside it, or it carries the item's own name and overlaps the hit box (a hit area
        // moved off its face to stay clear of a neighbour). An item cut by its window has its
        // face checked like any other (the face is registered whether or not the window shows it).
        bool has_face = false;
        for (const ui::audit::Face& f : forced_faces_) {
            const bool same_name = h.name.size() > f.name.size() &&
                                   h.name.compare(h.name.size() - f.name.size(), f.name.size(),
                                                  f.name) == 0 &&
                                   h.name[h.name.size() - f.name.size() - 1] == ' ';
            const bool overlaps = f.min.x < h.max.x && h.min.x < f.max.x && f.min.y < h.max.y &&
                                  h.min.y < f.max.y;
            if (inside(f.min, f.max, h.min, h.max) || inside(h.min, h.max, f.min, f.max) ||
                (same_name && overlaps)) {
                has_face = true;
                break;
            }
        }
        if (!has_face) {
            std::fprintf(out, "%s\t%s\tno-face\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t0\t0\tFAIL\n",
                         label_.c_str(), h.name.c_str(), static_cast<double>(h.min.x),
                         static_cast<double>(h.min.y), static_cast<double>(h.max.x),
                         static_cast<double>(h.max.y), static_cast<double>(h.max.x - h.min.x),
                         static_cast<double>(h.max.y - h.min.y));
        }
    }
}

} // namespace etude::app
