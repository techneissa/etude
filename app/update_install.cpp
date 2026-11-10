// SPDX-License-Identifier: GPL-3.0-or-later
// The install half of the update service's worker: download, signature, a look at the tarball's
// contents (so an error shows early), wait for the idle screen, hand over to the root helper. The app
// does not unpack anything: it leaves the tarball and its signature in the update folder and the
// helper, as root, copies them out, verifies them again and unpacks them itself (helper/). Every way
// it ends is a published view; nothing here runs on the frame thread. See update_service.hpp for the
// whole flow.
#include "update_service_shared.hpp"

#include "etude/semver.hpp"
#include "etude/tar_archive.hpp"
#include "etude/update_handoff.hpp"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>

namespace etude::app {

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t kMaxSignatureBytes = 4096;
constexpr std::size_t kMaxUnpackedBytes = 1024ULL * 1024 * 1024;
constexpr std::size_t kMaxStatusBytes = 2048;
constexpr auto kPoll = std::chrono::milliseconds(250);

std::string read_small(const fs::path& path, std::size_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    std::string text(max_bytes, '\0');
    in.read(text.data(), static_cast<std::streamsize>(max_bytes));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return text;
}

std::span<const std::uint8_t> bytes_of(const std::string& s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}

// What the app leaves for the helper: the two files and the request. Gone again when the update does
// not go ahead.
void remove_handoff(const fs::path& update_dir) {
    std::error_code ignore;
    for (const std::string_view name :
         {kUpdateRequestFile, kUpdateTarballFile, kUpdateSignatureFile}) {
        fs::remove(update_dir / std::string(name), ignore);
    }
}

} // namespace

FailureText UpdateService::Shared::last_update_failure() const {
    if (options.update_dir.empty()) {
        return {};
    }
    const UpdateStatus status =
        parse_update_status(
        read_small(options.update_dir / std::string(kUpdateStatusFile), kMaxStatusBytes));
    // Only an update that did not happen: one for a version still ahead of the running one.
    if (compare_versions(status.version, options.current_version) != VersionOrder::Newer) {
        return {};
    }
    return update_status_failure(status);
}

void UpdateService::Shared::write_ready() const {
    write_update_ready(options.update_dir, options.current_version);
}

bool UpdateService::Shared::stop_requested() {
    std::lock_guard lock(mutex);
    return quit || cancel_wanted;
}

void UpdateService::Shared::fail_install(const ui::UpdateView& base, FailureText why) {
    ui::UpdateView next = base;
    next.phase = ui::UpdatePhase::Failed;
    next.failure = std::move(why.full);
    next.failure_summary = std::move(why.summary);
    next.retry_is_check = false;
    next.cancellable = false;
    next.percent = 0;
    next.detail.clear();
    publish(std::move(next));
}

// The tarball and its signature into memory, with the percent of the first published as it comes.
bool UpdateService::Shared::fetch_release(const UpdateOffer& offer, ui::UpdateView& view,
                                          std::string& tarball, std::string& signature,
                                          bool& was_cancelled, FailureText& why) {
    int shown = -1;
    const std::uint64_t expected = offer.info.size;
    const auto progress = [&](std::uint64_t got, std::uint64_t total) {
        if (stop_requested()) {
            return false;
        }
        const std::uint64_t whole = total != 0 ? total : expected;
        const int percent = whole == 0 ? 0 : static_cast<int>(std::min<std::uint64_t>(100, got * 100 / whole));
        if (percent != shown) {
            shown = percent;
            view.percent = percent;
            publish(view);
        }
        return true;
    };
    FetchResult file = options.fetcher->fetch(offer.pick.tarball.url, update_fetch_hosts(),
                                              expected, progress);
    if (file.cancelled) {
        was_cancelled = true;
        return false;
    }
    if (!file.ok) {
        why = fetch_failure_text(file);
        return false;
    }
    if (file.body.size() != expected) {
        why = {"The download is not the size the release said", "Wrong download size"};
        return false;
    }
    FetchResult sig = options.fetcher->fetch(
        offer.pick.signature.url, update_fetch_hosts(), kMaxSignatureBytes,
        [this](std::uint64_t, std::uint64_t) { return !stop_requested(); });
    if (sig.cancelled) {
        was_cancelled = true;
        return false;
    }
    if (!sig.ok) {
        why = fetch_failure_text(sig);
        return false;
    }
    tarball = std::move(file.body);
    signature = std::move(sig.body);
    return true;
}

// Looks into the checked tarball the way the helper will (core read_tar refuses anything an update
// has no business doing), then leaves the tarball and its signature in the update folder, whole and
// flushed, for the helper. Nothing is unpacked here.
bool UpdateService::Shared::leave_release(const UpdateOffer& offer, const std::string& tarball,
                                          const std::string& signature, FailureText& why) {
    const std::string& version = offer.release.version;
    GunzipResult unzipped = gunzip(bytes_of(tarball), kMaxUnpackedBytes);
    if (!unzipped.ok()) {
        why = {"The download could not be unpacked", "Download damaged"};
        return false;
    }
    const TarResult listed = read_tar(unzipped.data, "etude-" + version);
    if (!listed.ok()) {
        why = {"The download was refused: " + tar_error_text(listed), "Download refused"};
        return false;
    }
    std::error_code ec;
    fs::create_directories(options.update_dir, ec);
    remove_handoff(options.update_dir);
    int error = 0;
    if (!write_file_atomic(options.update_dir / std::string(kUpdateTarballFile), tarball, &error) ||
        !write_file_atomic(options.update_dir / std::string(kUpdateSignatureFile), signature,
                           &error)) {
        remove_handoff(options.update_dir);
        why = error == ENOSPC ? space_failure_text(tarball.size() + signature.size())
                              : FailureText{"The download could not be saved for the installer: " +
                                                std::error_code(error, std::generic_category())
                                                    .message(),
                                            "Couldn't save the download"};
        return false;
    }
    return true;
}

// Holds until nothing is playing, a run or a recording is on; false when cancelled or quitting.
bool UpdateService::Shared::wait_until_idle(ui::UpdateView& view) {
    bool said = false;
    std::unique_lock lock(mutex);
    while (!quit && !cancel_wanted) {
        if (!busy) {
            return true;
        }
        if (!said) {
            said = true;
            lock.unlock();
            view.detail = "Waiting until nothing is playing";
            publish(view);
            lock.lock();
            continue;
        }
        cv.wait_for(lock, kPoll, [this] { return quit || cancel_wanted || !busy; });
    }
    return false;
}

// Drops the request and waits for the helper: it stops the app to install, so what the loop below
// sees is its refusal, or a service that was not restarted.
bool UpdateService::Shared::hand_over(const UpdateOffer& offer, ui::UpdateView& view) {
    const std::string& version = offer.release.version;
    const fs::path status_file = options.update_dir / std::string(kUpdateStatusFile);
    std::error_code ec;
    fs::remove(status_file, ec);
    if (!write_file_atomic(options.update_dir / std::string(kUpdateRequestFile),
                           format_update_request(version))) {
        remove_handoff(options.update_dir);
        fail_install(view, {"The update request could not be written", "Couldn't start the update"});
        return false;
    }
    view.detail = "Restarting to install";
    view.cancellable = false;
    publish(view);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(options.pickup_wait_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::unique_lock lock(mutex);
            if (cv.wait_for(lock, kPoll, [this] { return quit; })) {
                return false;
            }
        }
        const UpdateStatus status = parse_update_status(read_small(status_file, kMaxStatusBytes));
        if (status.state == UpdateState::Failed || status.state == UpdateState::RolledBack) {
            remove_handoff(options.update_dir);
            fail_install(view, update_status_failure(status));
            return false;
        }
        if (status.state == UpdateState::Ok) {
            ui::UpdateView done = view;
            done.phase = ui::UpdatePhase::UpToDate;
            done.detail = "Restart Etude to use version " + version;
            done.cancellable = false;
            publish(done);
            return true;
        }
    }
    remove_handoff(options.update_dir);
    fail_install(view, {"The installer did not pick the update up", "Installer not responding"});
    return false;
}

// Takes the offer to install and publishes the Downloading view; false when there is none.
bool UpdateService::Shared::begin_install(UpdateOffer& offered, ui::UpdateView& base,
                                          ui::UpdateView& view) {
    {
        std::lock_guard lock(mutex);
        offered = found;
    }
    if (!offered.valid || options.update_dir.empty()) {
        return false;
    }
    base = current();
    view = base;
    view.phase = ui::UpdatePhase::Downloading;
    view.version = offered.release.version;
    view.percent = 0;
    view.failure.clear();
    view.detail.clear();
    view.cancellable = true;
    publish(view);
    return true;
}

void UpdateService::Shared::run_install() {
    if (!options.mac.bundle.empty()) {
        run_install_mac();
        return;
    }
    UpdateOffer offered;
    ui::UpdateView base;
    ui::UpdateView view;
    if (!begin_install(offered, base, view)) {
        return;
    }
    const auto give_up = [&](FailureText why) { fail_install(base, std::move(why)); };
    const auto cancel_back = [&] {
        remove_handoff(options.update_dir);
        publish(base);
    };

    std::string tarball;
    std::string signature;
    bool was_cancelled = false;
    FailureText why;
    if (!fetch_release(offered, view, tarball, signature, was_cancelled, why)) {
        was_cancelled ? cancel_back() : give_up(std::move(why));
        return;
    }
    view.phase = ui::UpdatePhase::Installing;
    view.detail = "Checking the download";
    publish(view);
    // The signature over the tarball's own bytes is what decides; release.json only pointed here.
    const SignatureCheck checked =
        options.verify(bytes_of(tarball), signature, release_trusted_comment(offered.info));
    if (!checked.ok()) {
        give_up(signature_failure_text(checked.error));
        return;
    }
    view.detail = "Preparing the install";
    publish(view);
    if (!leave_release(offered, tarball, signature, why)) {
        give_up(std::move(why));
        return;
    }
    tarball = std::string();
    if (!wait_until_idle(view)) {
        cancel_back();
        return;
    }
    hand_over(offered, view);
}

} // namespace etude::app
