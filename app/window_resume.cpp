// SPDX-License-Identifier: GPL-3.0-or-later
// The resume point in the window run (core resume_point.hpp, resume_keeper.hpp): read at the start to
// reopen the screen, song, bar, section, hand and tempo the player left, kept while the run goes, and
// marked clean at a normal close.
#include "window_state.hpp"

#include "log_ring.hpp"

#include <chrono>

namespace etude::app {
namespace {

std::int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

// After the settings are read. A developer run (a script, the audit, a headless window) reads and keeps
// nothing, so its shots and its steps do not depend on what a person left.
void WindowBase::start_resume() {
    const bool keeps = !developer_run(opt) && opt.song_path.empty();
    std::optional<ResumeParse> read;
    if (keeps) {
        read = read_resume_file(data_dir / kResumeFile);
    }
    // The folders a song may be reopened from: the library's and the data folder.
    std::vector<std::filesystem::path> folders{data_dir};
    folders.insert(folders.end(), library.roots().begin(), library.roots().end());
    resume_action = etude::plan_resume(read ? &*read : nullptr, resume_song_roots(folders));
    etude::ResumePoint first;
    first.session = model.session;
    if (resume_action == etude::ResumeAction::Resume) {
        resume_loaded = read->point;
        model.session = etude::session_with_resume(model.session, resume_loaded);
        first = resume_loaded;
        log_printf("etude: resuming the %s screen%s\n", resume_loaded.screen.c_str(),
                   resume_loaded.session.has_song() ? " and its song" : "");
    } else if (resume_action == etude::ResumeAction::SongRefused) {
        // The point names a file outside the folders songs live in: it is not reopened.
        model.session = etude::session_without_song(model.session);
        first.session = model.session;
        first.screen = "library";
        pending_toast.set("Etude could not reopen that song, so it opened the song list", true);
        log_printf("etude: the resume point names a song outside the song folders: opening the song list\n");
    } else if (resume_action == etude::ResumeAction::OpenSongList) {
        // The run before it was closed by a crash within seconds of its start: what it reopened is the
        // likely cause, so this start opens the song list, with the settings as they are.
        model.session = etude::session_without_song(model.session);
        first.session = model.session;
        first.screen = "library";
        pending_toast.set("Etude closed soon after it started, so it opened the song list", true);
        log_printf("etude: the last run ended within %d s of its start: opening the song list\n",
                   etude::kResumeGuardSeconds);
    }
    resume.begin(data_dir / kResumeFile, keeps, first, steady_ms());
}

// Every frame once the song the point names is in (while it is still being read the point keeps what
// it has, so a crash in that read leaves what the next start reopens).
void WindowState::keep_resume_point() {
    if (!resume.active() || restore_waiting || saved_restore.active() || pending_open.has_value()) {
        return;
    }
    resume_scratch = model.session;
    songs.write_session_state(resume_scratch);
    sections.save_into(resume_scratch);
    resume.note(resume_scratch, ui.resume_screen(), steady_ms());
}

void WindowState::close_resume_point() {
    if (!resume.active()) {
        return;
    }
    resume.close(model.session, ui.resume_screen(), steady_ms());
}

} // namespace etude::app
