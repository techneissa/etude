// SPDX-License-Identifier: GPL-3.0-or-later
#include "persistence.hpp"

#include <doctest/doctest.h>

#include "etude/renderer_path.hpp"
#include "etude/session_state.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace etude;
using etude::app::saved_renderer;
namespace fs = std::filesystem;

TEST_CASE("renderer path names parse and round-trip") {
    for (const RendererPath path : {RendererPath::Basic, RendererPath::Gpu, RendererPath::Gl}) {
        RendererPath parsed = RendererPath::Basic;
        CHECK(parse_renderer_path(renderer_path_name(path), &parsed));
        CHECK(parsed == path);
    }
    RendererPath untouched = RendererPath::Gl;
    CHECK_FALSE(parse_renderer_path("vulkan", &untouched));
    CHECK_FALSE(parse_renderer_path("", &untouched));
    CHECK(untouched == RendererPath::Gl);
}

TEST_CASE("The renderer is basic by default, saved as renderer, and a bad value keeps basic") {
    CHECK(AppSettings{}.renderer == RendererPath::Basic);
    for (const RendererPath path : {RendererPath::Gpu, RendererPath::Gl}) {
        AppSettings a;
        a.renderer = path;
        SettingsDocument doc = SettingsDocument::parse("");
        write_app_settings(doc, a);
        const std::string text = doc.serialize();
        CHECK(text.find(path == RendererPath::Gpu ? "renderer = gpu\n" : "renderer = gl\n") !=
              std::string::npos);
        CHECK(read_app_settings(SettingsDocument::parse(text), nullptr).renderer == path);
    }
    std::vector<SettingsIssue> issues;
    const AppSettings bad =
        read_app_settings(SettingsDocument::parse("[app]\nrenderer = vulkan\n"), &issues);
    CHECK(bad.renderer == RendererPath::Basic);
    CHECK_FALSE(issues.empty());
}

TEST_CASE("saved_renderer reads the renderer before the window exists") {
    std::random_device rd;
    const fs::path dir =
        fs::temp_directory_path() / ("etude-renderer-test-" + std::to_string(rd()));
    fs::create_directories(dir);
    CHECK(saved_renderer(dir) == RendererPath::Basic); // no file
    const auto write = [&](const char* text) {
        std::ofstream out(dir / "settings.conf", std::ios::binary);
        out << text;
    };
    write("[app]\nrenderer = gl\n");
    CHECK(saved_renderer(dir) == RendererPath::Gl);
    write("[app]\nrenderer = vulkan\n");
    CHECK(saved_renderer(dir) == RendererPath::Basic); // an unknown name keeps the default
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("renderer path bits are one each, so a set of paths holds them apart") {
    unsigned all = 0;
    for (const RendererPath path : {RendererPath::Basic, RendererPath::Gpu, RendererPath::Gl}) {
        CHECK((all & renderer_path_bit(path)) == 0);
        all |= renderer_path_bit(path);
    }
    CHECK(all == 0b111);
}
