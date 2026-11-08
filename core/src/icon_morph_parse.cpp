// SPDX-License-Identifier: GPL-3.0-or-later
// Morphing stroke icons, after the technique of morphicons (https://github.com/guillermolg00/

#include "icon_morph_internal.hpp"
// morphicons, MIT License, Copyright (c) 2026 Guillermo). A port of the technique, not the code.
#include "etude/icon_morph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace etude {

using namespace morph_detail;

namespace {

bool same(MorphPoint a, MorphPoint b) { return a.x == b.x && a.y == b.y; }
class Builder {
  public:
    explicit Builder(CubicPath& out) : out_(out) {}

    PathError move(MorphPoint p) {
        finish(false);
        cur_ = p;
        start_ = p;
        open_ = false;
        return PathError::None;
    }

    PathError cubic(MorphPoint c1, MorphPoint c2, MorphPoint p3) {
        if (!open_) {
            if (out_.subpath_count >= kMaxSubpaths) {
                return PathError::TooManySubpaths;
            }
            out_.subpaths[static_cast<std::size_t>(out_.subpath_count)] = {out_.cubic_count, 0,
                                                                           false};
            start_ = cur_;
            open_ = true;
        }
        if (same(cur_, c1) && same(c1, c2) && same(c2, p3)) {
            return PathError::None; // a segment of no length adds nothing
        }
        if (out_.cubic_count >= kMaxCubics) {
            return PathError::TooManyCubics;
        }
        out_.cubics[static_cast<std::size_t>(out_.cubic_count++)] = {cur_, c1, c2, p3};
        ++out_.subpaths[static_cast<std::size_t>(out_.subpath_count)].count;
        cur_ = p3;
        return PathError::None;
    }

    PathError line(MorphPoint p) {
        return cubic(lerp(cur_, p, 1.0f / 3.0f), lerp(cur_, p, 2.0f / 3.0f), p);
    }

    PathError close() {
        if (open_ && !same(cur_, start_)) {
            if (const PathError e = line(start_); e != PathError::None) {
                return e;
            }
        }
        finish(true);
        cur_ = start_;
        return PathError::None;
    }

    // Ends the open subpath; one with no segments is dropped.
    void finish(bool closed) {
        if (!open_) {
            return;
        }
        open_ = false;
        CubicSubpath& s = out_.subpaths[static_cast<std::size_t>(out_.subpath_count)];
        if (s.count > 0) {
            s.closed = closed;
            ++out_.subpath_count;
        }
    }

    MorphPoint cur() const { return cur_; }

  private:
    CubicPath& out_;
    MorphPoint cur_;
    MorphPoint start_;
    bool open_ = false;
};
// SVG arc, endpoint form to centre form, in slices of at most 90 degrees.
PathError arc(Builder& b, float rx_in, float ry_in, float phi_deg, bool large, bool sweep,
              MorphPoint to) {
    const MorphPoint from = b.cur();
    if (same(from, to)) {
        return PathError::None;
    }
    double rx = std::fabs(static_cast<double>(rx_in));
    double ry = std::fabs(static_cast<double>(ry_in));
    if (rx == 0.0 || ry == 0.0) {
        return b.line(to);
    }
    const double phi = static_cast<double>(phi_deg) * kPi / 180.0;
    const double cp = std::cos(phi);
    const double sp = std::sin(phi);
    const double dx = (static_cast<double>(from.x) - to.x) * 0.5;
    const double dy = (static_cast<double>(from.y) - to.y) * 0.5;
    const double x1 = cp * dx + sp * dy;
    const double y1 = -sp * dx + cp * dy;
    const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1.0) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double coef = den > 0.0 ? std::sqrt(std::max(0.0, num / den)) : 0.0;
    if (large == sweep) {
        coef = -coef;
    }
    const double cxp = coef * rx * y1 / ry;
    const double cyp = -coef * ry * x1 / rx;
    const double cx = cp * cxp - sp * cyp + (static_cast<double>(from.x) + to.x) * 0.5;
    const double cy = sp * cxp + cp * cyp + (static_cast<double>(from.y) + to.y) * 0.5;
    const double ux = (x1 - cxp) / rx;
    const double uy = (y1 - cyp) / ry;
    const double vx = (-x1 - cxp) / rx;
    const double vy = (-y1 - cyp) / ry;
    const double start = std::atan2(uy, ux);
    double delta = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0.0) {
        delta -= 2.0 * kPi;
    } else if (sweep && delta < 0.0) {
        delta += 2.0 * kPi;
    }
    const int slices =
        std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / (kPi * 0.5) - 1e-9)));
    const double step = delta / slices;
    const double alpha = 4.0 / 3.0 * std::tan(step / 4.0);
    const auto map = [&](double u, double v) {
        return MorphPoint{static_cast<float>(cx + cp * rx * u - sp * ry * v),
                          static_cast<float>(cy + sp * rx * u + cp * ry * v)};
    };
    for (int i = 0; i < slices; ++i) {
        const double a0 = start + step * i;
        const double a1 = a0 + step;
        const double c0 = std::cos(a0);
        const double s0 = std::sin(a0);
        const double c1 = std::cos(a1);
        const double s1 = std::sin(a1);
        const MorphPoint end = i + 1 == slices ? to : map(c1, s1);
        if (const PathError e = b.cubic(map(c0 - alpha * s0, s0 + alpha * c0),
                                        map(c1 + alpha * s1, s1 - alpha * c1), end);
            e != PathError::None) {
            return e;
        }
    }
    return PathError::None;
}
class Reader {
  public:
    explicit Reader(std::string_view s) : s_(s) {}

    void skip() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == ',' || s_[i_] == '\t' ||
                                  s_[i_] == '\n' || s_[i_] == '\r')) {
            ++i_;
        }
    }
    bool done() {
        skip();
        return i_ >= s_.size();
    }
    // A command letter next.
    bool at_command() {
        skip();
        if (i_ >= s_.size()) {
            return false;
        }
        const char c = s_[i_];
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }
    char command() { return s_[i_++]; }

    bool number(float& out) {
        skip();
        const std::size_t begin = i_;
        if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) {
            ++i_;
        }
        bool digits = false;
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
            ++i_;
            digits = true;
        }
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
                ++i_;
                digits = true;
            }
        }
        if (!digits) {
            i_ = begin;
            return false;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            std::size_t j = i_ + 1;
            if (j < s_.size() && (s_[j] == '+' || s_[j] == '-')) {
                ++j;
            }
            if (j < s_.size() && s_[j] >= '0' && s_[j] <= '9') {
                while (j < s_.size() && s_[j] >= '0' && s_[j] <= '9') {
                    ++j;
                }
                i_ = j;
            }
        }
        out = parse(s_.substr(begin, i_ - begin));
        return true;
    }

    // An arc flag: a single 0 or 1, which may be packed against the next number.
    bool flag(bool& out) {
        skip();
        if (i_ < s_.size() && (s_[i_] == '0' || s_[i_] == '1')) {
            out = s_[i_++] == '1';
            return true;
        }
        return false;
    }

  private:
    // No locale and no allocation: digits, a fraction and an exponent.
    static float parse(std::string_view t) {
        std::size_t i = 0;
        double sign = 1.0;
        if (t[i] == '+' || t[i] == '-') {
            sign = t[i] == '-' ? -1.0 : 1.0;
            ++i;
        }
        double v = 0.0;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
            v = v * 10.0 + (t[i++] - '0');
        }
        if (i < t.size() && t[i] == '.') {
            ++i;
            double scale = 0.1;
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
                v += (t[i++] - '0') * scale;
                scale *= 0.1;
            }
        }
        if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
            ++i;
            int esign = 1;
            if (t[i] == '+' || t[i] == '-') {
                esign = t[i] == '-' ? -1 : 1;
                ++i;
            }
            int e = 0;
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
                e = std::min(e * 10 + (t[i++] - '0'), 400);
            }
            v *= std::pow(10.0, esign * e);
        }
        return static_cast<float>(sign * v);
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

} // namespace

PathError parse_svg_path(std::string_view d, CubicPath& out) {
    Builder b(out);
    Reader r(d);
    // An error keeps what parsed before it, the subpath in progress too.
    const auto fail = [&b](PathError e) {
        b.finish(false);
        return e;
    };
    char cmd = 0;
    MorphPoint last_control; // the previous C/S or Q/T control point, for the shorthands
    char last_kind = 0;      // 'C' or 'Q' when last_control is valid
    while (!r.done()) {
        if (r.at_command()) {
            cmd = r.command();
        } else if (cmd == 0) {
            return fail(PathError::Syntax);
        }
        const bool rel = cmd >= 'a' && cmd <= 'z';
        const char up = static_cast<char>(rel ? cmd - 'a' + 'A' : cmd);
        const MorphPoint at = b.cur();
        const auto pt = [&](float x, float y) {
            return rel ? MorphPoint{at.x + x, at.y + y} : MorphPoint{x, y};
        };
        PathError e = PathError::None;
        char kind = 0;
        float v[6];
        switch (up) {
        case 'Z':
            e = b.close();
            cmd = 0; // a number after Z is an error, unless a command follows
            break;
        case 'M':
            if (!r.number(v[0]) || !r.number(v[1])) {
                return fail(PathError::Syntax);
            }
            e = b.move(pt(v[0], v[1]));
            cmd = rel ? 'l' : 'L'; // pairs after a move are lines
            break;
        case 'L':
            if (!r.number(v[0]) || !r.number(v[1])) {
                return fail(PathError::Syntax);
            }
            e = b.line(pt(v[0], v[1]));
            break;
        case 'H':
            if (!r.number(v[0])) {
                return fail(PathError::Syntax);
            }
            e = b.line({rel ? at.x + v[0] : v[0], at.y});
            break;
        case 'V':
            if (!r.number(v[0])) {
                return fail(PathError::Syntax);
            }
            e = b.line({at.x, rel ? at.y + v[0] : v[0]});
            break;
        case 'C':
            for (float& x : v) {
                if (!r.number(x)) {
                    return fail(PathError::Syntax);
                }
            }
            last_control = pt(v[2], v[3]);
            e = b.cubic(pt(v[0], v[1]), last_control, pt(v[4], v[5]));
            kind = 'C';
            break;
        case 'S': {
            for (int i = 0; i < 4; ++i) {
                if (!r.number(v[i])) {
                    return fail(PathError::Syntax);
                }
            }
            const MorphPoint c1 = last_kind == 'C' ? MorphPoint{2.0f * at.x - last_control.x,
                                                                2.0f * at.y - last_control.y}
                                                   : at;
            last_control = pt(v[0], v[1]);
            e = b.cubic(c1, last_control, pt(v[2], v[3]));
            kind = 'C';
            break;
        }
        case 'Q':
        case 'T': {
            MorphPoint q;
            MorphPoint end;
            if (up == 'Q') {
                for (int i = 0; i < 4; ++i) {
                    if (!r.number(v[i])) {
                        return fail(PathError::Syntax);
                    }
                }
                q = pt(v[0], v[1]);
                end = pt(v[2], v[3]);
            } else {
                if (!r.number(v[0]) || !r.number(v[1])) {
                    return fail(PathError::Syntax);
                }
                q = last_kind == 'Q'
                        ? MorphPoint{2.0f * at.x - last_control.x, 2.0f * at.y - last_control.y}
                        : at;
                end = pt(v[0], v[1]);
            }
            last_control = q;
            // Degree elevation, exact.
            e = b.cubic(lerp(at, q, 2.0f / 3.0f), lerp(end, q, 2.0f / 3.0f), end);
            kind = 'Q';
            break;
        }
        case 'A': {
            bool large = false;
            bool sweep = false;
            if (!r.number(v[0]) || !r.number(v[1]) || !r.number(v[2]) || !r.flag(large) ||
                !r.flag(sweep) || !r.number(v[3]) || !r.number(v[4])) {
                return fail(PathError::Syntax);
            }
            e = arc(b, v[0], v[1], v[2], large, sweep, pt(v[3], v[4]));
            break;
        }
        default:
            return fail(PathError::Syntax);
        }
        if (e != PathError::None) {
            return fail(e);
        }
        last_kind = kind;
    }
    b.finish(false);
    return PathError::None;
}

PathError add_circle(CubicPath& out, float cx, float cy, float r) {
    Builder b(out);
    b.move({cx + r, cy});
    if (const PathError e = arc(b, r, r, 0.0f, false, true, {cx - r, cy}); e != PathError::None) {
        return e;
    }
    if (const PathError e = arc(b, r, r, 0.0f, false, true, {cx + r, cy}); e != PathError::None) {
        return e;
    }
    return b.close();
}

PathError add_rect(CubicPath& out, float x, float y, float w, float h, float rx) {
    rx = std::clamp(rx, 0.0f, std::min(w, h) * 0.5f);
    Builder b(out);
    b.move({x + rx, y});
    const MorphPoint corners[4][2] = {{{x + w - rx, y}, {x + w, y + rx}},
                                      {{x + w, y + h - rx}, {x + w - rx, y + h}},
                                      {{x + rx, y + h}, {x, y + h - rx}},
                                      {{x, y + rx}, {x + rx, y}}};
    for (const auto& c : corners) {
        PathError e = b.line(c[0]);
        if (e == PathError::None && rx > 0.0f) {
            e = arc(b, rx, rx, 0.0f, false, true, c[1]);
        }
        if (e != PathError::None) {
            return e;
        }
    }
    return b.close();
}

} // namespace etude
