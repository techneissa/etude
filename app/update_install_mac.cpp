// SPDX-License-Identifier: GPL-3.0-or-later
// The Mac's install, in the update service's worker: download the zip of the app bundle, check its
// signature from a hash read in pieces, wait for the idle screen, unpack it beside the running
// bundle, check the unpacked bundle, then start the relaunch script and ask the app to close. The
// script (mac_relaunch_script.hpp) does the swap and the rollback after the app has gone. Every way
// it ends before the quit is a published view; nothing here runs on the frame thread.
#include "log_ring.hpp"
#include "mac_update.hpp"
#include "update_service_shared.hpp"

#include "etude/update_handoff.hpp"

#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <system_error>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kZipFile = "release.zip";

} // namespace

// The signature covers the BLAKE2b-512 hash of the zip, so the zip is saved in the update folder and
// read back in pieces for the hash, the way the Pi's helper reads its copy (a signature of the older
// kind, over the bytes themselves, is refused by the check). release-macos.json is only a pointer;
// this signature decides.
bool UpdateService::Shared::check_mac_zip(const UpdateOffer& offer, const std::string& zip,
                                          const std::string& signature, ui::UpdateView& view,
                                          FailureText& why) {
    std::error_code ec;
    fs::create_directories(options.update_dir, ec);
    int error = 0;
    const fs::path file = options.update_dir / std::string(kZipFile);
    if (!write_file_atomic(file, zip, &error)) {
        why = error == ENOSPC ? space_failure_text(zip.size())
                              : FailureText{"The download could not be saved: " +
                                                std::error_code(error, std::generic_category()).message(),
                                            "Couldn't save the download"};
        return false;
    }
    Digest512 digest{};
    if (!digest_of_file(file, digest)) {
        why = {"The download could not be read back for its signature check", "Couldn't read the download"};
        return false;
    }
    const SignatureCheck checked =
        options.verify_digest(digest, signature, release_trusted_comment(offer.info));
    if (!checked.ok()) {
        why = signature_failure_text(checked.error);
        return false;
    }
    view.detail = "Preparing the install";
    publish(view);
    return true;
}

// Unpacks the checked zip beside the running bundle and checks what came out.
bool UpdateService::Shared::prepare_mac_bundle(const UpdateOffer& offer, ui::UpdateView& view,
                                               fs::path& staged, FailureText& why) {
    const std::string& version = offer.release.version;
    const ProgramRunner run = options.mac.run ? options.mac.run : ProgramRunner(run_program);
    view.detail = "Unpacking";
    publish(view);
    const fs::path staging = options.mac.bundle.parent_path() / mac_staging_name(version);
    const StepResult unpacked = unpack_release_zip(run, options.update_dir / std::string(kZipFile), staging);
    if (!unpacked.ok) {
        why = {unpacked.why, "Download damaged"};
        return false;
    }
    staged = find_unpacked_bundle(staging);
    const StepResult checked = staged.empty()
                                   ? StepResult{false, "The download does not hold one app"}
                                   : check_new_bundle(run, staged, version);
    if (!checked.ok) {
        std::error_code ec;
        fs::remove_all(staging, ec);
        why = {"The download was refused: " + checked.why, "Download refused"};
        return false;
    }
    return true;
}

// Hands over to the relaunch script, which outlives the app: the swap, the start and the rollback
// are its.
bool UpdateService::Shared::start_mac_relaunch(const UpdateOffer& offer, ui::UpdateView& view,
                                               const fs::path& staged, FailureText& why) {
    RelaunchPlan plan;
    plan.app_pid = options.mac.pid != 0 ? options.mac.pid : static_cast<long>(::getpid());
    plan.bundle = options.mac.bundle;
    plan.new_bundle = staged;
    plan.previous = options.mac.bundle.parent_path() / mac_previous_name(options.current_version);
    plan.update_dir = options.update_dir;
    plan.version = offer.release.version;
    plan.from = options.current_version;
    plan.ready_wait_s = options.mac.ready_wait_s;
    view.detail = "Restarting to finish the install";
    view.cancellable = false;
    publish(view);
    const StepResult started = start_relaunch(plan, options.mac.spawn);
    if (!started.ok) {
        why = {started.why, "Couldn't start the update"};
        return false;
    }
    return true;
}

void UpdateService::Shared::run_install_mac() {
    UpdateOffer offered;
    {
        std::lock_guard lock(mutex);
        offered = found;
    }
    if (!offered.valid || options.update_dir.empty()) {
        return;
    }
    const ui::UpdateView base = current();
    ui::UpdateView view = base;
    view.phase = ui::UpdatePhase::Downloading;
    view.version = offered.release.version;
    view.percent = 0;
    view.failure.clear();
    view.detail.clear();
    view.cancellable = true;
    publish(view);
    const fs::path zip_file = options.update_dir / std::string(kZipFile);
    const fs::path staging = options.mac.bundle.parent_path() / mac_staging_name(offered.release.version);
    const auto clean = [&] {
        std::error_code ec;
        fs::remove(zip_file, ec);
        fs::remove_all(staging, ec);
    };
    const auto give_up = [&](FailureText why) {
        clean();
        fail_install(base, std::move(why));
    };
    const auto cancel_back = [&] {
        clean();
        publish(base);
    };

    std::string zip;
    std::string signature;
    bool was_cancelled = false;
    FailureText why;
    if (!fetch_release(offered, view, zip, signature, was_cancelled, why)) {
        was_cancelled ? cancel_back() : give_up(std::move(why));
        return;
    }
    view.phase = ui::UpdatePhase::Installing;
    view.detail = "Checking the download";
    publish(view);
    const bool checked = check_mac_zip(offered, zip, signature, view, why);
    std::string().swap(zip); // the zip is on disk now
    if (!checked) {
        give_up(std::move(why));
        return;
    }
    if (!wait_until_idle(view)) {
        cancel_back();
        return;
    }
    fs::path staged;
    if (!prepare_mac_bundle(offered, view, staged, why)) {
        give_up(std::move(why));
        return;
    }
    std::error_code ec;
    fs::remove(zip_file, ec);
    if (stop_requested()) {
        cancel_back();
        return;
    }
    if (!start_mac_relaunch(offered, view, staged, why)) {
        give_up(std::move(why));
        return;
    }
    log_printf("update: version %s unpacked beside the app; closing for the swap\n",
               offered.release.version.c_str());
    if (options.mac.quit) {
        options.mac.quit();
    }
}

} // namespace etude::app
