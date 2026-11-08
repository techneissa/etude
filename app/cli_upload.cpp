// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli_upload.hpp"

#include "folder_watcher.hpp"
#include "song_lengths.hpp"
#include "song_library.hpp"
#include "upload_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

namespace etude::app {

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true); }

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

} // namespace

int run_upload_serve(const WindowOptions& opt) {
    namespace fs = std::filesystem;
    const fs::path data_dir = settings_directory(opt.settings_dir);
    UploadServerOptions options;
    options.songs = data_dir / "Songs";
    options.removed = data_dir / "Removed songs";
    options.port = opt.upload_port;
    if (!opt.upload_bind.empty()) {
        options.bind = {opt.upload_bind};
    }
    options.all_interfaces = opt.upload_all_interfaces;
    options.request_deadline = std::chrono::seconds(opt.upload_deadline_s);
    // The songs' lengths, read once from the Songs folder as the app's library would (this mode has
    // no library of its own that keeps up with new files).
    SongLibrary library({options.songs});
    library.scan();
    library.load_all_now();
    library.poll();
    SongLengthBook lengths;
    lengths.set(song_lengths_of(library));
    options.song_length = [&lengths](const fs::path& file) { return lengths.get(file); };
    UploadServer server(options);
    std::string error;
    if (!server.start(&error)) {
        std::fprintf(stderr, "upload: %s\n", error.c_str());
        return 1;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::printf("upload: PIN %s on port %d, files go to %s/Inbox\n", server.pin().c_str(),
                server.port(), options.songs.string().c_str());
    for (const std::string& ip : server.addresses()) {
        std::printf("upload: listening on http://%s:%d\n", ip.c_str(), server.port());
    }
    std::printf("upload: Ctrl-C stops it; it also stops after 10 idle minutes\n");
    std::fflush(stdout);
    UploadStatus last = server.status();
    while (!g_stop.load() &&
           (server.status() == UploadStatus::Ready || server.status() == UploadStatus::Receiving)) {
        sleep_ms(200);
        if (server.status() != last) {
            last = server.status();
            std::printf("upload: %s\n", last == UploadStatus::Receiving ? "receiving" : "ready");
            std::fflush(stdout);
        }
    }
    std::printf("upload: stopped%s%s\n", server.reason().empty() ? "" : " - ",
                server.reason().c_str());
    server.stop();
    return 0;
}

int run_watch_log(const std::string& dir) {
    FolderWatcher::Options options;
    options.root = dir;
    FolderWatcher watcher(options);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    sleep_ms(300); // the worker picks its way of watching
    std::printf("watch: %s (%s)\n", dir.c_str(), watcher.backend());
    std::fflush(stdout);
    while (!g_stop.load()) {
        if (const auto report = watcher.take()) {
            for (const std::string& path : report->added) {
                std::printf("watch: added   %s\n", path.c_str());
            }
            for (const std::string& path : report->changed) {
                std::printf("watch: changed %s\n", path.c_str());
            }
            for (const std::string& path : report->removed) {
                std::printf("watch: removed %s\n", path.c_str());
            }
            if (report->moved > 0) {
                std::printf("watch: moved   %zu\n", report->moved);
            }
            if (report->new_songs > 0) {
                std::printf("watch: toast \"%s\"\n", new_songs_text(report->new_songs).c_str());
            }
            std::fflush(stdout);
        }
        sleep_ms(100);
    }
    return 0;
}

} // namespace etude::app
