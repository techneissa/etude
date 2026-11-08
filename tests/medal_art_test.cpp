// SPDX-License-Identifier: GPL-3.0-or-later
// The medals as pixels: the right canvas, the disc opaque and the corners clear, every material its
// own look, the small size without a ribbon, and the same pixels every time (medal_art.hpp).
#include "etude/medal_art.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>

using namespace etude;

namespace {

struct Mean {
    double r = 0;
    double g = 0;
    double b = 0;
    double luma() const { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
};

std::uint8_t alpha_at(const MedalPixels& m, int x, int y) {
    return m.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)) * 4 + 3];
}

// The mean colour of the face's pixels, a ring of the disc's face around its centre.
Mean face_mean(const MedalPixels& m, const MedalFrame& f) {
    Mean sum;
    int n = 0;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - f.centre_x;
            const float dy = static_cast<float>(y) + 0.5f - f.centre_y;
            if (std::sqrt(dx * dx + dy * dy) < f.face * 0.9f) {
                const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)) * 4;
                sum.r += m.rgba[at];
                sum.g += m.rgba[at + 1];
                sum.b += m.rgba[at + 2];
                ++n;
            }
        }
    }
    return {sum.r / n, sum.g / n, sum.b / n};
}

} // namespace

TEST_CASE("medal art: the canvas is the frame's size, the disc is solid and the far corners are clear") {
    for (const MedalSize size : {MedalSize::Large, MedalSize::Small}) {
        const MedalFrame f = medal_frame(size);
        for (std::size_t i = 0; i < kMaterialCount; ++i) {
            const MedalPixels m = render_medal(static_cast<Material>(i), size);
            REQUIRE(m.width == f.width);
            REQUIRE(m.height == f.height);
            REQUIRE(m.rgba.size() == static_cast<std::size_t>(f.width * f.height * 4));
            CAPTURE(i);
            CHECK(alpha_at(m, static_cast<int>(f.centre_x), static_cast<int>(f.centre_y)) == 255);
            CHECK(alpha_at(m, static_cast<int>(f.centre_x + f.radius * 0.9f), static_cast<int>(f.centre_y)) == 255);
            CHECK(alpha_at(m, 0, f.height - 1) == 0);
            CHECK(alpha_at(m, f.width - 1, f.height - 1) == 0);
        }
        CHECK(f.radius < static_cast<float>(f.width) * 0.5f);
        CHECK(f.face < f.radius);
        CHECK(f.centre_y + f.radius <= static_cast<float>(f.height));
    }
}

TEST_CASE("medal art: the large medal hangs from a ribbon over the disc, the small one is the disc alone") {
    const MedalFrame large = medal_frame(MedalSize::Large);
    const MedalFrame small = medal_frame(MedalSize::Small);
    const MedalPixels big = render_medal(Material::Gold, MedalSize::Large);
    const MedalPixels little = render_medal(Material::Gold, MedalSize::Small);
    // Above the disc, on the ribbon's straps, the large one is opaque.
    CHECK(alpha_at(big, static_cast<int>(large.centre_x) - 36, 2) == 255);
    CHECK(alpha_at(big, static_cast<int>(large.centre_x) + 36, 2) == 255);
    CHECK(large.centre_y - large.radius > 60.0f); // room for the ribbon
    // The small canvas has the disc and a margin for its shadow, no more.
    CHECK(small.centre_y - small.radius < 8.0f);
    CHECK(alpha_at(little, 2, 2) == 0);
}

TEST_CASE("medal art: no ring above the disc: the straps run on behind it and no end shows") {
    const MedalFrame f = medal_frame(MedalSize::Large);
    const int cx = static_cast<int>(f.centre_x);
    const int ring_cy = static_cast<int>(f.centre_y - f.radius - 4.0f); // where the eyelet ring sat
    for (std::size_t i = 0; i < kMaterialCount; ++i) {
        const MedalPixels m = render_medal(static_cast<Material>(i), MedalSize::Large);
        const auto px = [&](int x, int y, int c) { return m.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)]; };
        CAPTURE(i);
        // The ring's band (8 px above its centre) used to be lit rim metal around a hole of ribbon; now both are the same strap.
        for (int c = 0; c < 4; ++c) {
            CHECK(std::abs(static_cast<int>(px(cx, ring_cy - 8, c)) - static_cast<int>(px(cx, ring_cy - 2, c))) < 40);
        }
        // The straps are opaque down to the disc's top edge: no gap where they met the ring.
        CHECK(alpha_at(m, cx, ring_cy) == 255);
        CHECK(alpha_at(m, cx, static_cast<int>(f.centre_y - f.radius) - 1) == 255);
    }
    // The small size draws no ribbon and no ring: the pixel above its disc is clear.
    const MedalFrame sf = medal_frame(MedalSize::Small);
    const MedalPixels little = render_medal(Material::Gold, MedalSize::Small);
    CHECK(alpha_at(little, static_cast<int>(sf.centre_x), 0) == 0);
}

TEST_CASE("medal art: no halo reaches the frame's edge: the outer ring of pixels is the shadow's, as on a medal with no glow") {
    for (const MedalSize size : {MedalSize::Large, MedalSize::Small}) {
        const MedalFrame f = medal_frame(size);
        const MedalPixels plain = render_medal(Material::Wood, size); // no glow: the shadow alone at the edge
        for (std::size_t i = 0; i < kMaterialCount; ++i) {
            const MedalPixels m = render_medal(static_cast<Material>(i), size);
            CAPTURE(i);
            for (int y = 0; y < m.height; ++y) {
                for (int x = 0; x < m.width; ++x) {
                    if (x != 0 && y != 0 && x != m.width - 1 && y != m.height - 1) {
                        continue;
                    }
                    // The large medal's ribbon runs off the top edge; the rest of the ring is shadow or clear.
                    if (size == MedalSize::Large && y == 0 && std::fabs(static_cast<float>(x) - f.centre_x) < 60.0f) {
                        continue;
                    }
                    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)) * 4;
                    for (std::size_t c = 0; c < 4; ++c) {
                        CHECK(m.rgba[at + c] == plain.rgba[at + c]);
                    }
                }
            }
        }
    }
}

TEST_CASE("medal art: the same pixels every time") {
    for (std::size_t i = 0; i < kMaterialCount; ++i) {
        CHECK(render_medal(static_cast<Material>(i), MedalSize::Large).rgba ==
              render_medal(static_cast<Material>(i), MedalSize::Large).rgba);
        CHECK(render_medal(static_cast<Material>(i), MedalSize::Small).rgba ==
              render_medal(static_cast<Material>(i), MedalSize::Small).rgba);
    }
}

TEST_CASE("medal art: every material has its own look: the faces differ and have the right character") {
    const MedalFrame f = medal_frame(MedalSize::Large);
    std::array<Mean, kMaterialCount> mean{};
    for (std::size_t i = 0; i < kMaterialCount; ++i) {
        mean[i] = face_mean(render_medal(static_cast<Material>(i), MedalSize::Large), f);
    }
    for (std::size_t i = 0; i < kMaterialCount; ++i) {
        for (std::size_t j = i + 1; j < kMaterialCount; ++j) {
            const double d = std::fabs(mean[i].r - mean[j].r) + std::fabs(mean[i].g - mean[j].g) + std::fabs(mean[i].b - mean[j].b);
            CAPTURE(i);
            CAPTURE(j);
            CHECK(d > 12.0); // no two materials look alike
        }
    }
    const auto m = [&](Material x) { return mean[static_cast<std::size_t>(x)]; };
    CHECK(m(Material::Obsidian).luma() < m(Material::Platinum).luma()); // deep, but not dull: see the next case
    CHECK(m(Material::Obsidian).b > m(Material::Obsidian).g + 15);   // violet glass
    CHECK(m(Material::Iron).luma() < m(Material::Silver).luma());
    CHECK(m(Material::Silver).luma() > m(Material::Bronze).luma());
    CHECK(m(Material::Gold).r > m(Material::Gold).b + 30);   // warm
    CHECK(m(Material::Bronze).r > m(Material::Bronze).b + 30);
    CHECK(m(Material::Wood).r > m(Material::Wood).b + 20);
    CHECK(m(Material::Diamond).b > m(Material::Diamond).r + 20); // ice blue
    CHECK(m(Material::Platinum).b > m(Material::Platinum).r);    // cool: platinum leans blue, silver does not
    CHECK(m(Material::Platinum).b - m(Material::Platinum).r > m(Material::Silver).b - m(Material::Silver).r + 10);
}

TEST_CASE("medal art: the disc is lit from the top left: brighter there than at the lower right") {
    const MedalFrame f = medal_frame(MedalSize::Large);
    for (const Material material : {Material::Silver, Material::Gold, Material::Bronze}) {
        const MedalPixels m = render_medal(material, MedalSize::Large);
        const auto luma_at = [&](float fx, float fy) {
            const int x = static_cast<int>(f.centre_x + fx * f.radius);
            const int y = static_cast<int>(f.centre_y + fy * f.radius);
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)) * 4;
            return 0.2126 * m.rgba[at] + 0.7152 * m.rgba[at + 1] + 0.0722 * m.rgba[at + 2];
        };
        CHECK(luma_at(-0.35f, -0.35f) > luma_at(0.35f, 0.35f));
    }
}

TEST_CASE("medal art: the icon colours read on the face: the fill differs from it, light above dark") {
    const MedalFrame f = medal_frame(MedalSize::Large);
    for (std::size_t i = 0; i < kMaterialCount; ++i) {
        const MedalLook look = medal_look(static_cast<Material>(i));
        const auto luma = [](std::uint32_t c) {
            return 0.2126 * ((c >> 16) & 0xFF) + 0.7152 * ((c >> 8) & 0xFF) + 0.0722 * (c & 0xFF);
        };
        CAPTURE(i);
        CHECK(luma(look.emboss_light) > luma(look.emboss_dark));
        const Mean face = face_mean(render_medal(static_cast<Material>(i), MedalSize::Large), f);
        CHECK(std::fabs(luma(look.icon_fill) - face.luma()) > 8.0); // the icon stands out of its face
        CHECK(look.ribbon != 0);
    }
}

TEST_CASE("medal art: obsidian is the top tier: a strong highlight, a gold ring, a gold-edged ribbon, a violet glow") {
    const MedalFrame f = medal_frame(MedalSize::Large);
    const MedalPixels m = render_medal(Material::Obsidian, MedalSize::Large);
    const auto at = [&](int x, int y) { return (static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)) * 4; };
    const auto luma = [&](int x, int y) {
        const std::size_t i = at(x, y);
        return 0.2126 * m.rgba[i] + 0.7152 * m.rgba[i + 1] + 0.0722 * m.rgba[i + 2];
    };
    // The sweep of light: somewhere on the face is far brighter than the face's mean.
    double brightest = 0.0;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - f.centre_x;
            const float dy = static_cast<float>(y) + 0.5f - f.centre_y;
            if (std::sqrt(dx * dx + dy * dy) < f.face * 0.9f) {
                brightest = std::max(brightest, luma(x, y));
            }
        }
    }
    const Mean mean = face_mean(m, f);
    CHECK(brightest > mean.luma() * 2.0);
    CHECK(brightest > 150.0);
    // The thin ring where the rim starts is gold: red and green over blue, along the disc's left side.
    bool gold_ring = false;
    for (int x = static_cast<int>(f.centre_x - f.radius); x < static_cast<int>(f.centre_x); ++x) {
        const std::size_t i = at(x, static_cast<int>(f.centre_y));
        if (m.rgba[i] > 120 && m.rgba[i + 1] > 85 && m.rgba[i] > m.rgba[i + 2] + 20) {
            gold_ring = true;
        }
    }
    CHECK(gold_ring);
    // The halo: just outside the rim, to the left where no shadow falls, the pixel is faintly translucent violet.
    const std::size_t g = at(static_cast<int>(f.centre_x - f.radius) - 2, static_cast<int>(f.centre_y));
    CHECK(m.rgba[g + 3] > 15);
    CHECK(m.rgba[g + 3] < 60);
    CHECK(m.rgba[g + 2] > m.rgba[g + 1]);
}
