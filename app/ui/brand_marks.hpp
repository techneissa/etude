// SPDX-License-Identifier: GPL-3.0-or-later
// The pictures of the About page: etude's own icon in each of its colours and the two brand marks
// beside its links (the GitHub mark and the Buy Me a Coffee cup, app/assets/brands/). Each is
// decoded once, made as large as the pixels it covers (so it is as sharp as the text beside it) into
// a texture, and drawn by `draw_brand` or `draw_app_icon`. The GitHub mark is one colour and takes
// the colour it is drawn in; the others are drawn as published.
#pragma once

#include "etude/icon_colour.hpp"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace etude::app::ui {

struct Kit;

enum class Brand : std::uint8_t { GitHub, BuyMeACoffee };

// Where an app icon is drawn: each place keeps its own texture, since each is a different size.
enum class IconSlot : std::uint8_t { Header, Swatch };

// Area-average downscale of ARGB pixels (alpha-weighted) from `sw` x `sh` to `dw` x `dh`, both
// smaller than the source; a larger target returns the source unchanged. Pure, so a test holds it.
std::vector<std::uint32_t> downscale_argb(const std::vector<std::uint32_t>& src, int sw, int sh,
                                          int dw, int dh);

// Draws `brand` in the square `side` points across, centred on `centre`. `tint` colours the GitHub
// mark only. Nothing is drawn while the icon audit leaves icons out.
void draw_brand(const Kit& k, ImDrawList* dl, Brand brand, ImVec2 centre, float side, ImVec4 tint);
// Draws the app icon in `colour` in the square `side` points across, centred on `centre`, `alpha`
// opaque. Nothing is drawn while the icon audit leaves icons out.
void draw_app_icon(const Kit& k, ImDrawList* dl, etude::IconColour colour, IconSlot slot,
                   ImVec2 centre, float side, float alpha = 1.0f);
// Frees the cached textures; call while the renderer is alive.
void release_brands();

} // namespace etude::app::ui
