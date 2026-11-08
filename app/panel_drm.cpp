// SPDX-License-Identifier: GPL-3.0-or-later
#include "panel_drm.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace etude::app {
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxEdidBytes = 1024; // a base block and a few extensions

std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return line;
}

std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> out;
    char c = 0;
    while (out.size() < kMaxEdidBytes && in.get(c)) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
    return out;
}

struct Candidate {
    std::string name;
    PanelMm mm;
    bool mode_matches = false;
};

} // namespace

std::optional<ProbedPanel> probe_drm_panel(const std::string& root, int px_w, int px_h) {
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("card", 0) == 0 && name.find('-') != std::string::npos) {
            dirs.push_back(e.path());
        }
    }
    std::sort(dirs.begin(), dirs.end());
    const std::string want = std::to_string(px_w) + "x" + std::to_string(px_h);
    std::vector<Candidate> found;
    for (const fs::path& dir : dirs) {
        if (read_text(dir / "status") != "connected") {
            continue;
        }
        const std::vector<std::uint8_t> edid = read_bytes(dir / "edid");
        const std::optional<PanelMm> raw = panel_mm_from_edid(edid.data(), edid.size());
        if (!raw) {
            continue;
        }
        const std::optional<PanelMm> mm =
            plausible_panel_mm(*raw, static_cast<float>(px_w), static_cast<float>(px_h));
        if (!mm) {
            continue;
        }
        found.push_back({dir.filename().string(), *mm, read_text(dir / "modes") == want});
    }
    if (found.empty()) {
        return std::nullopt;
    }
    const auto best = std::find_if(found.begin(), found.end(),
                                   [](const Candidate& c) { return c.mode_matches; });
    const Candidate& pick = best != found.end() ? *best : found.front();
    return ProbedPanel{pick.mm, PanelSource::Edid, pick.name};
}

} // namespace etude::app
