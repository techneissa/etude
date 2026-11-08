// SPDX-License-Identifier: GPL-3.0-or-later
// The display's size from DRM connectors' EDID under a sysfs-like folder (app/panel_drm.hpp): the
// connected connector with a believable size, the one whose mode fits, and the ones skipped.
#include "panel_drm.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;
namespace fs = std::filesystem;

namespace {

std::array<std::uint8_t, 128> edid(int w_mm, int h_mm) {
    std::array<std::uint8_t, 128> b{};
    const std::uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    for (int i = 0; i < 8; ++i) {
        b[static_cast<std::size_t>(i)] = header[i];
    }
    b[54] = 0x02;
    b[55] = 0x3A;
    b[66] = static_cast<std::uint8_t>(w_mm & 0xFF);
    b[67] = static_cast<std::uint8_t>(h_mm & 0xFF);
    b[68] = static_cast<std::uint8_t>(((w_mm >> 8) << 4) | (h_mm >> 8));
    return b;
}

class Sysfs {
  public:
    Sysfs() : root_(fs::temp_directory_path() / ("etude-drm-" + std::to_string(counter()++))) {
        fs::create_directories(root_);
    }
    ~Sysfs() { fs::remove_all(root_); }
    Sysfs(const Sysfs&) = delete;
    Sysfs& operator=(const Sysfs&) = delete;

    void connector(const std::string& name, const std::string& status, const std::string& mode,
                   const std::array<std::uint8_t, 128>* block) {
        const fs::path dir = root_ / name;
        fs::create_directories(dir);
        std::ofstream(dir / "status") << status << "\n";
        std::ofstream(dir / "modes") << mode << "\n1280x720\n";
        std::ofstream out(dir / "edid", std::ios::binary);
        if (block != nullptr) {
            out.write(reinterpret_cast<const char*>(block->data()),
                      static_cast<std::streamsize>(block->size()));
        }
    }
    std::string root() const { return root_.string(); }

  private:
    static int& counter() {
        static int n = 0;
        return n;
    }
    fs::path root_;
};

} // namespace

TEST_CASE("drm panel: the connected connector's EDID gives the size") {
    Sysfs sys;
    const auto pi = edid(344, 194);
    sys.connector("card1-HDMI-A-1", "connected", "1920x1080", &pi);
    const auto got = probe_drm_panel(sys.root(), 1920, 1080);
    REQUIRE(got.has_value());
    CHECK(got->mm.w == 344.0f);
    CHECK(got->mm.h == 194.0f);
    CHECK(got->source == PanelSource::Edid);
    CHECK(got->detail == "card1-HDMI-A-1");
}

TEST_CASE("drm panel: a disconnected connector is ignored") {
    Sysfs sys;
    const auto old = edid(527, 296);
    sys.connector("card1-HDMI-A-1", "disconnected", "1920x1080", &old);
    CHECK_FALSE(probe_drm_panel(sys.root(), 1920, 1080).has_value());
}

TEST_CASE("drm panel: a panel with no EDID (a DSI screen) gives nothing") {
    Sysfs sys;
    sys.connector("card1-DSI-1", "connected", "800x480", nullptr);
    CHECK_FALSE(probe_drm_panel(sys.root(), 800, 480).has_value());
}

TEST_CASE("drm panel: of two connected, the one whose mode is the window's wins") {
    Sysfs sys;
    const auto small = edid(154, 86);
    const auto big = edid(344, 194);
    sys.connector("card1-DSI-1", "connected", "800x480", &small);
    sys.connector("card1-HDMI-A-1", "connected", "1920x1080", &big);
    const auto got = probe_drm_panel(sys.root(), 1920, 1080);
    REQUIRE(got.has_value());
    CHECK(got->mm.w == 344.0f);
    // With no mode that fits the believable ones, the first (by name) is taken.
    const auto first = probe_drm_panel(sys.root(), 1000, 600);
    REQUIRE(first.has_value());
    CHECK(first->detail == "card1-DSI-1");
}

TEST_CASE("drm panel: a size that does not fit the pixels is skipped") {
    Sysfs sys;
    const auto tiny = edid(35, 20); // 1920 px over 35 mm: a placeholder, not a panel
    sys.connector("card1-HDMI-A-1", "connected", "1920x1080", &tiny);
    CHECK_FALSE(probe_drm_panel(sys.root(), 1920, 1080).has_value());
}

TEST_CASE("drm panel: a missing folder gives nothing") {
    CHECK_FALSE(probe_drm_panel("/nonexistent/etude/drm", 1920, 1080).has_value());
}
