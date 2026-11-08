// SPDX-License-Identifier: GPL-3.0-or-later
// The profiles of the window run: what the switcher is shown, and what the app does when the player
// switches, adds or edits one (#79). A switch saves the settings of the person leaving under their
// profile, makes the other the history's active profile, puts that person's settings in the model,
// and brings back the song they left.
#include "window_state.hpp"

#include "configs_demo.hpp"
#include "icon_standing.hpp"

#include "etude/progress.hpp"
#include "etude/profile_settings.hpp"

#include <algorithm>
#include <ctime>
#include <span>
#include <string>
#include <vector>

namespace etude::app {

namespace {

std::int32_t today_key() {
    const std::time_t wall = std::time(nullptr);
    return etude::local_day_from_utc(static_cast<std::int64_t>(wall), local_utc_offset_s(wall));
}

// One scored run of `hash` on `day` for the Board tab's shots: `hit` of `graded` notes at `speed`.
etude::RunRecord board_run(std::uint64_t hash, std::int32_t day, int score, int graded, int hit,
                             int speed) {
    etude::RunRecord r;
    r.song_hash = hash;
    r.song_title = "Bach Prelude in C";
    r.local_day = day;
    r.speed_percent = speed;
    r.start_utc_s = static_cast<std::int64_t>(day) * 86400 + 52000 + score;
    r.end_utc_s = r.start_utc_s + 90;
    r.duration_ms = 90 * 1000;
    r.counts.perfect = hit;
    r.counts.missed = graded - hit;
    r.longest_combo = hit;
    r.all = {graded, hit};
    r.covered_percent = 100;
    r.score = score;
    return etude::sanitized(r);
}

// The household's runs of the open song for the Board tab: Anna climbs to 933 today (the run the
// demo card is about), Ben is ahead of her, Mum and Dani played slower and longer ago.
void seed_board_runs(HistoryStore& history, std::uint64_t hash, std::int32_t today,
                     std::int64_t anna, std::span<const std::int64_t> others) {
    // The seventh, 933, is the demo card's own run (seed_card_run).
    const int climb[] = {640, 702, 655, 780, 811, 885};
    for (int i = 0; i < 6; ++i) {
        history.add_run(board_run(hash, today - 6 + i, climb[i], 38, 30, 100), {}, etude::RunEvents{},
                        anna);
    }
    if (others.size() >= 3) {
        history.add_run(board_run(hash, today - 7, 780, 40, 32, 75), {}, etude::RunEvents{}, others[0]);
        history.add_run(board_run(hash, today - 2, 1012, 33, 30, 100), {}, etude::RunEvents{}, others[1]);
        history.add_run(board_run(hash, today - 10, 412, 33, 20, 50), {}, etude::RunEvents{}, others[2]);
    }
}

std::string refusal_text(etude::ProfileResult result) {
    switch (result) {
    case etude::ProfileResult::EmptyName:
        return "Give the profile a name";
    case etude::ProfileResult::NameTaken:
        return "Another profile has that name";
    case etude::ProfileResult::TooMany:
        return "A household holds up to " + std::to_string(etude::kMaxProfiles) + " profiles";
    case etude::ProfileResult::NotReady:
        return "The profiles are still loading";
    case etude::ProfileResult::NoSuchProfile:
        return "That profile is gone";
    case etude::ProfileResult::LastProfile:
        return "The only profile can't be deleted";
    case etude::ProfileResult::FirstProfile:
        return "The first profile can't be deleted";
    case etude::ProfileResult::NotSaved:
        return "The history can't be changed now (another Etude has it open), so nothing was deleted";
    case etude::ProfileResult::Ok:
        break;
    }
    return {};
}

} // namespace

HistoryOptions history_options_for(const std::filesystem::path& data_dir) {
    HistoryOptions options;
    options.profile_id = saved_active_profile(data_dir);
    return options;
}

// Each profile's streak, worked out when the panel shows and the runs or the day changed: the
// panel's tiles show it, and nothing else on the frame needs it.
void WindowBase::refresh_profile_streaks() {
    const std::size_t people = history.profiles().size();
    const std::size_t total = history.run_count_all() + people; // cheap: no copy of the runs each frame
    const std::int32_t today = today_key();
    if (total == streaks_key && today == streaks_day && profile_streaks.size() == people) {
        return;
    }
    streaks_key = total;
    streaks_day = today;
    profile_streaks.clear();
    profile_freezes.clear();
    for (const etude::ProfileRuns& p : history.household_runs()) {
        const etude::Streak streak = etude::profile_streak(p.runs, today);
        profile_streaks.push_back(streak.current);
        profile_freezes.push_back(streak.freezes);
    }
}

// The local day, looked up once a second at most (the clock is read and converted, not for every
// frame a card is up).
std::int32_t WindowBase::cached_today() {
    const std::int64_t now = etude::app::frame_clock_ns();
    if (today_read_ns == 0 || now - today_read_ns >= kTodayEveryNs) {
        today_read_ns = now;
        today_cached = today_key();
    }
    return today_cached;
}

// The Board tab of the summary page: the boards of the song the card's run was of, rebuilt only when
// the card, the runs or the day change. Nothing while the history loads: the runs are not all in.
void WindowBase::fill_board_info(BoardInfo& board, std::uint32_t card_serial,
                                 const etude::RunRecord& run) {
    if (history.loading()) {
        return;
    }
    const std::int32_t today = cached_today();
    const std::size_t total = history.run_count_all();
    if (song_board.stale(run.song_hash, today, history.active_profile(), card_serial, total)) {
        song_board.refresh(history.household_runs(), run.song_hash, today, history.active_profile(),
                           card_serial, total, &run);
    }
    board.periods = {song_board.board(etude::Period::Week), song_board.board(etude::Period::Month),
                     song_board.board(etude::Period::AllTime)};
    board.profiles = &history.profiles();
    board.active = history.active_profile();
    board.today = today;
    board.chart_view = model.app.runs_chart_view;
}

// The developer's card for the Board tab's shots: its invented run is one of Anna's runs of the song
// today, 933 points (590 for --summary-low, 680 for --summary-lowest, or, for --summary-short, five
// notes that do not count), kept in the history as a finished run is.
void WindowBase::seed_card_run(etude::RunRecord& record) {
    const std::int32_t today = today_key();
    record.song_title = "Bach Prelude in C";
    record.local_day = today;
    record.start_utc_s = static_cast<std::int64_t>(today) * 86400 + 60000;
    record.end_utc_s = record.start_utc_s + 90;
    record.duration_ms = 90 * 1000;
    record.speed_percent = 100;
    if (!opt.summary_short) {
        record.score = opt.summary_lowest ? 680 : opt.summary_low ? 590 : 933;
        record.covered_percent = 100;
    }
    record = etude::sanitized(record);
    history.add_run(record, {}, etude::RunEvents{}, history.active_profile());
}

// Each profile's title, from the same numbers the Progress page uses (the history thread works the
// medals out and caches them): asked for when the panel opens, the day turns or the household changes,
// and read off the answer when a new one is in.
void WindowBase::refresh_profile_titles(bool panel_open) {
    if (!panel_open) {
        titles_panel_open = false;
        return;
    }
    const std::int64_t active = history.active_profile();
    const int goal = model.app.daily_goal_minutes;
    const std::int32_t today = cached_today();
    const std::size_t people = history.profiles().size();
    if (!titles_panel_open) {
        titles_asked.reopen();
    }
    titles_panel_open = true;
    if (titles_asked.due(active, goal, today, people)) {
        library_view.request_progress(goal);
        titles_asked.note(active, goal, today, people);
    }
    const std::shared_ptr<const etude::ProgressView> view = library_view.progress();
    if (view == titles_from && profile_titles.size() == people) {
        return;
    }
    titles_from = view;
    profile_titles.assign(people, 0);
    if (view == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < people; ++i) {
        const std::int64_t id = history.profiles().all()[i].id;
        for (const etude::ProfileProgress& p : view->household) {
            if (p.profile_id == id) {
                profile_titles[i] = p.medals.title();
            }
        }
    }
}

// The best title of any profile, for the About page's colour row: asked of the history thread while
// the page is drawn (it asks through `icon_service.best_title()`), as the switcher's titles are.
void WindowBase::refresh_icon_title() {
    if (icon_service.fixed()) {
        return;
    }
    if (!icon_service.take_asked()) {
        icon_page_open = false;
        return;
    }
    const std::int64_t active = history.active_profile();
    const int goal = model.app.daily_goal_minutes;
    const std::int32_t today = cached_today();
    const std::size_t people = history.profiles().size();
    if (!icon_page_open) {
        icon_asked.reopen();
    }
    icon_page_open = true;
    if (icon_asked.due(active, goal, today, people)) {
        library_view.request_progress(goal);
        icon_asked.note(active, goal, today, people);
    }
    const std::shared_ptr<const etude::ProgressView> view = library_view.progress();
    if (view == icon_from) {
        return;
    }
    icon_from = view;
    const IconStanding standing = icon_standing(view.get());
    icon_service.set_best(standing.title, standing.points);
}

ProfileInfo WindowBase::profile_info(bool panel_open) {
    if (panel_open) {
        refresh_profile_streaks();
    }
    refresh_profile_titles(panel_open);
    ProfileInfo info;
    info.set = &history.profiles();
    info.looks = profile_look_cache.refresh(history, history.active_profile());
    info.streaks = profile_streaks;
    info.freezes = profile_freezes;
    info.titles = profile_titles;
    info.active = history.active_profile();
    info.ready = !history.loading();
    info.goal_minutes = model.app.daily_goal_minutes;
    info.progress = true;
    return info;
}

void WindowBase::profile_choice(const ProfileChoice& choice) {
    switch (choice.action) {
    case ProfileAction::None:
        return;
    case ProfileAction::Switch:
        switch_profile(choice.id);
        return;
    case ProfileAction::Add:
        add_profile(choice);
        return;
    case ProfileAction::Edit:
        edit_profile(choice);
        return;
    case ProfileAction::Delete:
        delete_profile(choice.id);
        return;
    case ProfileAction::Progress:
        return; // the window opens the library on its Progress page
    }
}

void WindowBase::add_profile(const ProfileChoice& choice) {
    etude::Profile made;
    const etude::ProfileResult result = history.add_profile(choice.name, &made);
    if (result != etude::ProfileResult::Ok) {
        show_message(refusal_text(result), true);
        return;
    }
    if (choice.colour != made.colour) {
        history.recolour_profile(made.id, choice.colour);
    }
    switch_profile(made.id);
    if (choice.goal_minutes >= 0) {
        model.app.daily_goal_minutes = choice.goal_minutes;
    }
    show_message("Added " + made.name);
}

void WindowBase::edit_profile(const ProfileChoice& choice) {
    const etude::Profile* now = history.profiles().find(choice.id);
    if (now == nullptr) {
        show_message(refusal_text(etude::ProfileResult::NoSuchProfile), true);
        return;
    }
    if (choice.name != now->name) {
        const etude::ProfileResult r = history.rename_profile(choice.id, choice.name);
        if (r != etude::ProfileResult::Ok) {
            show_message(refusal_text(r), true);
            return;
        }
    }
    if (choice.colour != now->colour) {
        history.recolour_profile(choice.id, choice.colour);
    }
    if (choice.goal_minutes >= 0 && choice.id == history.active_profile()) {
        model.app.daily_goal_minutes = choice.goal_minutes;
    }
}

// The profile and what is its own go (the confirm was tapped). The history thread deletes in one
// transaction and says so through poll_history (profile_deleted); a refusal (the only or the first
// profile, a history another etude holds) is said at once and nothing changes.
void WindowBase::delete_profile(std::int64_t id) {
    if (history.loading() || !history.profiles().contains(id)) {
        show_message(refusal_text(etude::ProfileResult::NoSuchProfile), true);
        return;
    }
    practice_sync.flush(); // choices still waiting are written before the deletion, not after it
    const etude::ProfileResult result = history.delete_profile(id);
    if (result != etude::ProfileResult::Ok) {
        show_message(refusal_text(result), true);
    }
}

// A deletion came to its end: done, the profile in use may have changed and the household and boards
// are asked again; or failed, and the profile is still there.
void WindowBase::profile_deleted(const etude::app::ProfileDeleted& gone) {
    if (!gone.done) {
        show_message("Could not delete " + gone.name + "; it is still there", true);
        return;
    }
    if (gone.switched) {
        apply_active_profile();
    }
    streaks_key = static_cast<std::size_t>(-1);
    const etude::Profile* now = history.profiles().find(history.active_profile());
    show_message("Deleted " + gone.name +
                 (gone.switched && now != nullptr ? ". Playing as " + now->name : std::string()));
}

void WindowBase::switch_profile(std::int64_t id) {
    const std::int64_t leaving = history.active_profile();
    if (id == leaving || history.loading() || !history.profiles().contains(id)) {
        return;
    }
    // What the person leaving chose is written under them first, the song's choices included.
    practice_sync.flush();
    history.set_profile_settings(leaving, persist.profile_text(to_save()));
    if (!history.switch_profile(id)) {
        return;
    }
    apply_active_profile();
    if (const etude::Profile* now = history.profiles().find(id)) {
        show_message("Playing as " + now->name);
    }
}

// The history's active profile is the one in use: its settings are in the model and the song it
// left comes back. Also what a start does when the saved profile is gone and the default one is
// used instead.
void WindowBase::apply_active_profile() {
    const std::int64_t id = history.active_profile();
    const std::int64_t now = midi_clock_ns();
    persist.apply_profile(model, history.profile_settings(id));
    model.app.active_profile = static_cast<int>(id); // the history still knows whose it is
    songs.set_profile(id, now); // a run in progress ends under the person who was playing
    results_card.reset();
    replay.forget_review();
    songs.set_options(song_options(), now);
    songs.set_count_in_bars(model.session.count_in_bars);
    practice_sync.set_accompaniment_default(model.app.accompaniment);
    sections.history_changed();
    streaks_key = static_cast<std::size_t>(-1);
    reopen_profile_song();
}

// The new person's song comes back as it does at a start; with none saved, the song that is open
// stays and opens again, so its speed, hands and key are this person's own.
void WindowBase::reopen_profile_song() {
    saved_restore.abandon();
    pending_open.reset();
    pending_index.reset();
    restore_cancelled = false;
    restore_deferred = false;
    restore_waiting = false;
    restore_scripted = false;
    if (model.session.has_song()) {
        const auto saved = library.resolve_song(model.session.song_path);
        const auto saved_abs = etude::app::split_song_path(model.session.song_abs_path);
        restore_target = saved ? saved
                               : (model.session.song_abs_path.empty()
                                      ? std::nullopt
                                      : std::optional<etude::app::SongPath>(saved_abs));
        songs.set_keyboard(session.layout.range(), midi_clock_ns());
        restore_step();
        return;
    }
    apply_saved_preferences();
    if (open_song_ref) {
        request_song(open_song_ref->path, open_song_ref->tune);
    }
}

namespace {

struct DemoSong {
    std::uint64_t hash;
    const char* title;
    int base; // the accuracy this piece comes to, in percent, before practice
};
constexpr DemoSong kDemoSongs[] = {{0x1234, "Bach Prelude in C", 74},
                                   {0x2345, "Canon in D", 70},
                                   {0x3456, "F\xC3\xBCr Elise", 78},
                                   {0x4567, "Air on the G String", 76},
                                   {0x5678, "Gymnop\xC3\xA9" "die No. 1", 66}};

// The minutes of a day's practice in the shots: a few short days, some under a 15 minute goal, some
// over it and a few long ones, so the calendar shows every step.
constexpr int kDemoMinutes[8] = {5, 11, 20, 38, 6, 24, 33, 16};

// A run of the household shots: `acc` percent of 40 notes hit, points that follow it, and the
// trouble bars the recent ones carry.
etude::RunRecord made_up_run(std::int32_t day, const DemoSong& song, int acc, int speed, bool bars) {
    etude::RunRecord r;
    r.song_hash = song.hash;
    r.song_title = song.title;
    r.speed_percent = speed;
    r.local_day = day;
    r.start_utc_s = static_cast<std::int64_t>(day) * 86400 + 40000;
    r.end_utc_s = r.start_utc_s + 900 + (acc % 7) * 120;
    r.duration_ms = (r.end_utc_s - r.start_utc_s) * 1000;
    const int hit = 40 * acc / 100;
    r.counts.perfect = hit * 3 / 4;
    r.counts.good = hit - r.counts.perfect;
    r.counts.missed = 40 - hit;
    r.all = {40, hit};
    r.covered_percent = 100;
    r.score = acc * 10 * (speed + 100) / 200;
    if (bars) {
        const int first = song.hash == 0x2345 ? 8 : song.hash == 0x3456 ? 12 : 3;
        const int count = song.hash == 0x2345 ? 1 : 3;
        for (int i = 0; i < count; ++i) {
            r.measures.push_back({first + i, 8, 4, 0});
        }
    }
    return r;
}

} // namespace

// Developer: the household of the profile and progress shots. The profile in use becomes Anna with a
// streak of twelve days and months of practice behind it; Mum, Ben and Dani are added with theirs.
void WindowBase::demo_profiles() {
    profiles_demo_done = true;
    const std::int32_t today = today_key();
    // `days`: the streak up to today, then `before` more days of older practice, most days.
    const auto practise = [&](std::int64_t id, int days, int before, int skill) {
        const int gap = days + 2; // a day without practice ends the streak
        const int first = days > 0 ? 0 : 3;
        for (int d = before + gap; d >= first; --d) {
            const bool streak = d < days;
            const bool older = d >= gap && (d * 7 + static_cast<int>(id) * 3) % 10 < 6;
            if (!streak && !older) {
                continue;
            }
            const DemoSong& song = kDemoSongs[(d * 3 + static_cast<int>(id)) % 5];
            const int growth = (before + gap - d) * 20 / (before + gap);
            const int acc = std::clamp(song.base - 14 + skill + growth + (d * 5) % 7, 30, 99);
            const int speed = d % 5 == 0 ? 80 : d % 7 == 0 ? 90 : 100;
            etude::RunRecord run = made_up_run(today - d, song, acc, speed, d < 3);
            if (d >= 3) { // the last days stay long enough to meet the goal; older ones vary
                run.duration_ms = kDemoMinutes[(d * 5 + static_cast<int>(id)) % 8] * 60 * 1000;
                run.end_utc_s = run.start_utc_s + run.duration_ms / 1000;
            }
            add_demo_run(id, std::move(run));
        }
    };
    history.rename_profile(etude::kDefaultProfileId, "Anna");
    history.recolour_profile(etude::kDefaultProfileId, etude::kProfileColours[2]);
    practise(etude::kDefaultProfileId, 12, 120, 6);
    struct Who {
        const char* name;
        std::size_t colour;
        int streak;
        int before;
        int skill;
    };
    std::vector<std::int64_t> others;
    for (const Who& who : {Who{"Mum", 0, 4, 60, 2}, Who{"Ben", 1, 0, 40, 5}, Who{"Dani", 3, 2, 30, -6}}) {
        etude::Profile made;
        const bool longest = opt.long_profile_name && who.streak == 2; // the last of them
        const char* name = longest ? "Wolfgang Amadeus Mozart the Youn" : who.name; // 32 bytes
        if (history.add_profile(name, &made) == etude::ProfileResult::Ok) {
            history.recolour_profile(made.id, etude::kProfileColours[who.colour]);
            // Each has a look of its own, for the Configs page's copy row.
            etude::SettingsDocument own;
            own.set_section_text("look", etude::serialize(configs_demo_look(3 + others.size())));
            history.set_profile_settings(made.id, etude::profile_settings_text(own));
            practise(made.id, who.streak, who.before, who.skill);
            others.push_back(made.id);
        }
    }
    if (opt.summary_demo && songs.has_song()) {
        seed_board_runs(history, songs.song().hash, today, etude::kDefaultProfileId, others);
    }
}

void WindowBase::add_demo_run(std::int64_t id, etude::RunRecord record) {
    history.add_run(etude::sanitized(record), {}, etude::RunEvents{}, id);
}

} // namespace etude::app
