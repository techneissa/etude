// SPDX-License-Identifier: GPL-3.0-or-later
// medal-sheet FILE.png [SCALE [icon]]: the eight medals, large and small, on the app's dark background (core
// medal_art.hpp). A look at the pixels the app puts in its texture atlas, without the app.
#include "etude/medal_art.hpp"
#include "etude/medals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::uint32_t crc_of(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0xFFFFFFFFU) {
    static std::array<std::uint32_t, 256> table{};
    if (table[1] == 0) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) {
        out.push_back(static_cast<std::uint8_t>((v >> s) & 0xFFU));
    }
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& body) {
    put32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> typed(type, type + 4);
    typed.insert(typed.end(), body.begin(), body.end());
    out.insert(out.end(), typed.begin(), typed.end());
    put32(out, ~crc_of(typed.data(), typed.size()));
}

// A PNG of RGBA8 with stored (uncompressed) deflate blocks.
std::vector<std::uint8_t> png_of(int w, int h, const std::vector<std::uint8_t>& rgba) {
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const auto* row = &rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4U];
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(w) * 4U);
    }
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    for (std::size_t at = 0; at < raw.size(); at += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFFU));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFFU));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFFU));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
    }
    put32(z, (b << 16) | a);
    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> head;
    put32(head, static_cast<std::uint32_t>(w));
    put32(head, static_cast<std::uint32_t>(h));
    head.insert(head.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", head);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

// A five-pointed star standing in for the badge icon: how the app lays an icon on the face, as an
// emboss (a dark edge down and right, a light one up and left, then the fill).
bool in_star(float x, float y, float r) {
    const float pi = 3.14159265f;
    const float a = std::atan2(y, x) + pi * 0.5f;
    const float seg = 2.0f * pi / 5.0f;
    const float t = std::fmod(a + 2.0f * pi, seg) / seg; // 0 .. 1 across one point
    const float outer = r;
    const float inner = r * 0.42f;
    const float edge = t < 0.5f ? outer + (inner - outer) * (t * 2.0f) : inner + (outer - inner) * ((t - 0.5f) * 2.0f);
    return std::sqrt(x * x + y * y) < edge;
}

void emboss_star(etude::MedalPixels& m, etude::Material material, etude::MedalSize size) {
    const etude::MedalFrame f = etude::medal_frame(size);
    const etude::MedalLook look = etude::medal_look(material);
    const float r = f.face * 0.66f;
    const float off = size == etude::MedalSize::Large ? 2.4f : 0.9f;
    const auto colour_of = [](std::uint32_t c, std::size_t i) {
        return static_cast<float>((c >> (16 - 8 * static_cast<int>(i))) & 0xFFU);
    };
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            const float px = static_cast<float>(x) + 0.5f - f.centre_x;
            const float py = static_cast<float>(y) + 0.5f - f.centre_y;
            const auto lay = [&](std::uint32_t colour, float dx, float dy) {
                if (!in_star(px - dx, py - dy, r)) {
                    return;
                }
                const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)) * 4U;
                for (std::size_t c = 0; c < 3; ++c) {
                    m.rgba[at + c] = static_cast<std::uint8_t>(colour_of(colour, c));
                }
            };
            lay(look.emboss_dark, off, off);
            lay(look.emboss_light, -off * 0.7f, -off * 0.7f);
            lay(look.icon_fill, 0.0f, 0.0f);
        }
    }
}

} // namespace

// medal-sheet --table: the catalogue as Markdown (the eight thresholds of each achievement, the titles
// and the overall rule).
int print_table() {
    std::printf("| Tier | Material | Title | Medals for the overall title |\n|---|---|---|---|\n");
    for (int t = 1; t <= etude::kTierCount; ++t) {
        std::printf("| %d | %s | %s | %d of %d |\n", t, std::string(etude::tier_material_name(t)).c_str(),
                    std::string(etude::tier_title(t)).c_str(), etude::title_points(t), etude::kMaxMedalPoints);
    }
    for (std::size_t c = 0; c < etude::kMedalCategoryCount; ++c) {
        const auto category = static_cast<etude::MedalCategory>(c);
        std::printf("\n### %s\n\n| Achievement | Rule | 1 Wood | 2 Iron | 3 Bronze | 4 Silver | 5 Gold | 6 Platinum | 7 Diamond | 8 Obsidian |\n|---|---|---|---|---|---|---|---|---|---|\n",
                    std::string(etude::medal_category_name(category)).c_str());
        for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
            const etude::AchievementInfo& a = etude::achievement_info(static_cast<etude::Achievement>(i));
            if (a.category != category) {
                continue;
            }
            std::printf("| %s | %s |", std::string(a.name).c_str(), [&] { std::string r(a.rule); const auto at = r.find("{n}"); if (at != std::string::npos) { r.replace(at, 3, "N"); } return r; }().c_str());
            for (int t = 0; t < etude::kTierCount; ++t) {
                const std::int64_t n = a.tiers[static_cast<std::size_t>(t)];
                std::printf(" %s |", a.unit == etude::MedalUnit::Percent ? (std::to_string(n) + "%").c_str() : etude::threshold_words(a, n).c_str());
            }
            std::printf("\n");
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--table") {
        return print_table();
    }
    if (argc == 2 && std::string(argv[1]) == "--looks") { // the icon colours of each material
        for (std::size_t i = 0; i < etude::kMaterialCount; ++i) {
            const etude::MedalLook l = etude::medal_look(static_cast<etude::Material>(i));
            std::printf("%zu %06X %06X %06X %06X\n", i, l.icon_fill, l.emboss_light, l.emboss_dark, l.ribbon);
        }
        return 0;
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: medal-sheet FILE.png [SCALE]\n");
        return 2;
    }
    const int scale = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1;
    const etude::MedalFrame large = etude::medal_frame(etude::MedalSize::Large);
    const etude::MedalFrame small = etude::medal_frame(etude::MedalSize::Small);
    const int pad = 12;
    const int cols = static_cast<int>(etude::kMaterialCount);
    const int w = (cols * (large.width + pad) + pad) * scale;
    const int h = (large.height + small.height + 3 * pad) * scale;
    std::vector<std::uint8_t> sheet(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
    const bool transparent = argc > 4; // the medals alone, for another tool to lay out
    for (std::size_t i = 0; i < sheet.size(); i += 4) {
        sheet[i] = 0x09;
        sheet[i + 1] = 0x0B;
        sheet[i + 2] = 0x11;
        sheet[i + 3] = transparent ? 0 : 0xFF;
    }
    const auto blit = [&](const etude::MedalPixels& m, int x0, int y0) {
        for (int y = 0; y < m.height * scale; ++y) {
            for (int x = 0; x < m.width * scale; ++x) {
                const std::size_t src = (static_cast<std::size_t>(y / scale) * static_cast<std::size_t>(m.width) +
                                         static_cast<std::size_t>(x / scale)) * 4U;
                const std::size_t dst = (static_cast<std::size_t>(y0 * scale + y) * static_cast<std::size_t>(w) +
                                         static_cast<std::size_t>(x0 * scale + x)) * 4U;
                const float al = static_cast<float>(m.rgba[src + 3]) / 255.0f;
                if (transparent) {
                    for (std::size_t c = 0; c < 4; ++c) {
                        sheet[dst + c] = m.rgba[src + c];
                    }
                    continue;
                }
                for (std::size_t c = 0; c < 3; ++c) {
                    sheet[dst + c] = static_cast<std::uint8_t>(static_cast<float>(m.rgba[src + c]) * al +
                                                               static_cast<float>(sheet[dst + c]) * (1.0f - al));
                }
            }
        }
    };
    for (int i = 0; i < cols; ++i) {
        const auto material = static_cast<etude::Material>(i);
        etude::MedalPixels big = etude::render_medal(material, etude::MedalSize::Large);
        etude::MedalPixels little = etude::render_medal(material, etude::MedalSize::Small);
        if (argc > 3) { // with the stand-in icon on the face
            emboss_star(big, material, etude::MedalSize::Large);
            emboss_star(little, material, etude::MedalSize::Small);
        }
        blit(big, pad + i * (large.width + pad), pad);
        blit(little, pad + i * (large.width + pad) + (large.width - small.width) / 2, 2 * pad + large.height);
    }
    const std::vector<std::uint8_t> png = png_of(w, h, sheet);
    std::FILE* f = std::fopen(argv[1], "wb");
    if (f == nullptr) {
        return 1;
    }
    std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    return 0;
}
