// SPDX-License-Identifier: GPL-3.0-or-later
// Switching a person's settings in and out of the model (persistence.hpp): the profile's part as
// text, another person's part put in, the device's part left alone, and the profile in use kept in
// the file that the history is opened with.
#include "persistence.hpp"

#include "etude/profile.hpp"

#include <doctest/doctest.h>

#include "sample_looks.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace etude;
using namespace etude::test;
using namespace etude::app;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("etude-profile-persist-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

UiModel anna() {
    UiModel m;
    m.look = sample_look(Sample::NeonNight);
    m.app.hit_window_ms = 120;
    m.app.daily_goal_minutes = 30;
    m.app.bar_pinned = true;
    m.app.latency_correction_ms = -35; // the device's
    m.app.midi_in_port = "Yamaha";     // the device's
    m.session.song_path = "Songs/Prelude.mid";
    m.session.song_hash = 77;
    return m;
}

} // namespace

TEST_CASE("profile text holds the person's settings and none of the device's") {
    TempDir dir;
    Persistence persist(dir.path);
    UiModel model;
    persist.load(model);
    model = anna();
    const std::string text = persist.profile_text(model);
    CHECK(text.find("hit_window_ms") != std::string::npos);
    CHECK(text.find("daily_goal_minutes") != std::string::npos);
    CHECK(text.find("song_hash") != std::string::npos);
    CHECK(text.find("latency_correction_ms") == std::string::npos);
    CHECK(text.find("midi_in_port") == std::string::npos);
    CHECK(text.find("active_profile") == std::string::npos);
}

TEST_CASE("applying a profile's text puts its settings in and keeps the device's") {
    TempDir dir;
    Persistence persist(dir.path);
    UiModel model;
    persist.load(model);
    model = anna();
    const std::string annas = persist.profile_text(model);
    // Ben's: his own hit window and goal; nothing else set.
    UiModel ben_model;
    ben_model.app.hit_window_ms = 60;
    ben_model.app.daily_goal_minutes = 10;
    Persistence other(dir.path / "other");
    fs::create_directories(dir.path / "other");
    UiModel scratch;
    other.load(scratch);
    const std::string bens = other.profile_text(ben_model);

    persist.apply_profile(model, bens);
    CHECK(model.app.hit_window_ms == 60);
    CHECK(model.app.daily_goal_minutes == 10);
    // What Ben never set is back at its default, not Anna's.
    CHECK_FALSE(model.app.bar_pinned);
    CHECK(model.session.song_path.empty());
    CHECK(model.look == LookSettings{}.clamped());
    // The device's part stays.
    CHECK(model.app.latency_correction_ms == -35);
    CHECK(model.app.midi_in_port == "Yamaha");

    persist.apply_profile(model, annas);
    CHECK(model.app.hit_window_ms == 120);
    CHECK(model.app.bar_pinned);
    CHECK(model.session.song_path == "Songs/Prelude.mid");
    CHECK(model.app.latency_correction_ms == -35);
}

TEST_CASE("an empty text is a profile that never set anything: every per-profile key is a default") {
    TempDir dir;
    Persistence persist(dir.path);
    UiModel model;
    persist.load(model);
    model = anna();
    persist.apply_profile(model, "");
    const AppSettings defaults;
    CHECK(model.app.hit_window_ms == defaults.hit_window_ms);
    CHECK(model.app.daily_goal_minutes == defaults.daily_goal_minutes);
    CHECK_FALSE(model.app.bar_pinned);
    CHECK(model.app.midi_in_port == "Yamaha");
}

TEST_CASE("the profile in use is saved with the device's settings and read before the history opens") {
    TempDir dir;
    CHECK(saved_active_profile(dir.path) == kDefaultProfileId); // no file yet
    Persistence persist(dir.path);
    UiModel model;
    persist.load(model);
    CHECK(model.app.active_profile == kDefaultProfileId);
    model.app.active_profile = 3;
    model.app.hit_window_ms = 90;
    CHECK(persist.flush(model).empty());
    CHECK(saved_active_profile(dir.path) == 3);
    // Switching the person's settings leaves the profile in use to the caller.
    persist.apply_profile(model, "");
    CHECK(model.app.active_profile == 3);
    model.app.active_profile = 2;
    CHECK(persist.flush(model).empty());
    CHECK(saved_active_profile(dir.path) == 2);

    Persistence again(dir.path);
    UiModel reread;
    again.load(reread);
    CHECK(reread.app.active_profile == 2);
}
