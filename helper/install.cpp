// SPDX-License-Identifier: GPL-3.0-or-later
#include "install.hpp"

#include "handoff_files.hpp"
#include "interrupt.hpp"
#include "layout.hpp"
#include "safe_io.hpp"
#include "state_store.hpp"
#include "stream.hpp"
#include "tree_check.hpp"
#include "unpack.hpp"

#include "etude/machine_os.hpp"
#include "etude/release_info.hpp"
#include "etude/semver.hpp"
#include "etude/tar_archive.hpp"
#include "etude/update_key.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>

namespace etude::helper {

namespace {

StageResult refuse(const std::string& why) {
    note("refused: " + why);
    return {false, why};
}

// What this machine is: its architecture, /etc/os-release and /etc/debian_version, read the way the
// app's check reads them (the files are root's, read here by name).
MachineOsText read_machine() {
    MachineOsText machine;
    machine.arch = build_debian_arch(); // this program's userland, never the kernel's
    // /etc/os-release is a link to /usr/lib/os-release on Debian: these are system files, root's, read
    // through links like any program does (the app's folder is where no link is followed).
    const auto read = [](const char* path, std::size_t max) {
        std::ifstream in(path, std::ios::binary);
        std::string text(max, '\0');
        in.read(text.data(), static_cast<std::streamsize>(max));
        text.resize(static_cast<std::size_t>(in.gcount()));
        return text;
    };
    parse_os_release(read("/etc/os-release", 16 * 1024), machine);
    machine.debian_version = first_line(read("/etc/debian_version", 256));
    return machine;
}

const char* signature_words(SignatureError error) {
    switch (error) {
    case SignatureError::KeyNotSet:
        return "updates are not set up in this build";
    case SignatureError::WrongKeyId:
        return "the download is signed by another key";
    case SignatureError::TrustedCommentMismatch:
        return "the download is signed for something else";
    default:
        return "the download did not pass its signature check";
    }
}

// Copies one of the app's files into the helper's folder; the app's own copy is removed after.
bool take_file(const Context& c, int update_fd, int incoming_fd, const char* name,
               std::uint64_t max_bytes, std::string& error) {
    FileLimits limits;
    limits.max_bytes = max_bytes;
    limits.check_owner = true;
    limits.owner = c.user.uid;
    PlainFile file = open_plain_file(update_fd, name, limits);
    if (file.status != OpenStatus::Ok) {
        error = std::string("the ") + name + " the app left was refused: " +
                (file.status == OpenStatus::Failed ? errno_text(file.error)
                                                   : open_status_text(file.status));
        return false;
    }
    std::string why;
    const bool copied = copy_to_new_file(file.fd.get(), incoming_fd, name, max_bytes, why);
    ::unlinkat(update_fd, name, 0);
    if (!copied) {
        error = "the " + std::string(name) + " could not be copied: " + why;
        return false;
    }
    return true;
}

// The signature file from the helper's copy (a few hundred bytes). The tarball is never read whole:
// `check_signature` hashes it in pieces.
bool read_signature_copy(int incoming_fd, std::string& out, std::string& error) {
    FileLimits limits;
    limits.max_bytes = kMaxSignatureBytes;
    PlainFile file = open_plain_file(incoming_fd, kSignatureFile, limits);
    std::vector<std::uint8_t> bytes;
    std::string why;
    if (file.status != OpenStatus::Ok || !read_all(file.fd.get(), kMaxSignatureBytes, bytes, why)) {
        error = std::string("the copy of ") + kSignatureFile + " could not be read";
        return false;
    }
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// The signature must be the project's over these very bytes, for this version and this machine. The
// tarball is hashed in pieces from `tarball_fd` (the helper's copy) and the signature covers the hash:
// the kind minisign writes by default. The old kind signs the bytes themselves, which would need the
// whole file in memory, so it is refused.
std::string check_signature(int tarball_fd, const std::string& sig, const std::string& version,
                            const std::string& running) {
    if (!update_key_ready()) {
        return signature_words(SignatureError::KeyNotSet);
    }
    // The comment is read from the file only to be handed to the check as what is expected: the
    // signature covers it, so a comment that was edited fails there.
    const std::string comment = signature_trusted_comment(sig);
    ReleaseComment said;
    if (!parse_release_trusted_comment(comment, said)) {
        return "the download's signed comment is not of the form etude VERSION OS ARCH";
    }
    if (!minisign_is_prehashed(sig)) {
        return "the download is signed the old way, which cannot be checked without holding it in memory";
    }
    Digest512 digest{};
    std::string why;
    if (!digest_file(tarball_fd, digest, why)) {
        return "the download could not be read for its signature check: " + why;
    }
    const SignatureCheck checked = verify_release_digest(digest, sig, comment);
    if (!checked.ok()) {
        return signature_words(checked.error);
    }
    if (said.version != version) {
        return "the download is signed for version " + said.version + ", not " + version;
    }
    const MachineOsText machine = read_machine();
    if (said.arch != machine.arch) {
        return "the download is built for " + said.arch + ", not for this machine";
    }
    if (!signed_os_fits_machine(said.os, machine.view())) {
        return "the download is built for " + said.os + " or newer, which this system is not";
    }
    if (compare_versions(version, running) != VersionOrder::Newer) {
        return version + " is not newer than " + running;
    }
    return {};
}

} // namespace

void discard_version(const Context& c, const std::string& version) {
    // Never the version `current` or `previous` points at: it is the one in use, or the way back. Looked
    // at here, just before the removal, not by whoever decided to discard it earlier.
    const auto in_use = [&] { return version_in_use(c, version); };
    if (in_use()) {
        note("not removing versions/" + version + ": `current` or `previous` points at it, or is not a link it can read");
        return;
    }
    std::string ignore;
    Fd versions = open_directory(c.versions_dir(), ignore);
    if (versions.ok()) {
        remove_tree_at(versions.get(), version + ".new", ignore);
        if (!in_use()) {
            remove_tree_at(versions.get(), version, ignore);
        }
    }
}

namespace {

// Removes the helper's copies of the download when the install is over, whatever way it ended.
class IncomingGuard {
  public:
    explicit IncomingGuard(int state_fd) : state_fd_(state_fd) {}
    ~IncomingGuard() {
        std::string ignore;
        remove_tree_at(state_fd_, "incoming", ignore);
    }
    IncomingGuard(const IncomingGuard&) = delete;
    IncomingGuard& operator=(const IncomingGuard&) = delete;

  private:
    int state_fd_;
};

} // namespace

namespace {

constexpr const char* kUnpackedTar = "release.tar";

// The signature and the tarball in the helper's folder: the signature checked from a hash of the
// tarball read in pieces, then the tarball unpacked into a file of the helper's own (gzip to tar) and
// that file read and listed by `read_tar`. `mapped` holds the tar's bytes; they are pages of that file,
// not memory the helper keeps.
StageResult check_and_list(int incoming_fd, const std::string& version, const std::string& running,
                           MappedFile& mapped, TarResult& listed) {
    std::string error;
    std::string signature;
    if (!read_signature_copy(incoming_fd, signature, error)) {
        return {false, error};
    }
    FileLimits limits;
    limits.max_bytes = kMaxTarballBytes;
    const PlainFile tarball = open_plain_file(incoming_fd, kReleaseFile, limits);
    if (tarball.status != OpenStatus::Ok) {
        return {false, std::string("the copy of ") + kReleaseFile + " could not be read"};
    }
    if (const std::string why = check_signature(tarball.fd.get(), signature, version, running);
        !why.empty()) {
        return {false, consume_interrupt() ? "the update was interrupted while its signature was checked" : why};
    }
    if (consume_interrupt()) {
        return {false, "the update was interrupted before it was unpacked"};
    }
    if (!gunzip_to_file(tarball.fd.get(), incoming_fd, kUnpackedTar, kMaxUnpackedBytes, error)) {
        return {false, consume_interrupt() ? "the update was interrupted while it was unpacked"
                                           : "the download could not be unpacked: " + error};
    }
    const PlainFile unpacked = open_plain_file(incoming_fd, kUnpackedTar, FileLimits{kMaxUnpackedBytes});
    if (unpacked.status != OpenStatus::Ok || !mapped.map(unpacked.fd.get(), error)) {
        return {false, "the unpacked download could not be read"};
    }
    listed = read_tar(mapped.bytes(), "etude-" + version);
    if (!listed.ok()) {
        return {false, tar_error_text(listed)};
    }
    return {true, {}};
}

} // namespace

StageResult stage_release(const Context& c, int update_fd, const std::string& version,
                          const std::string& running) {
    std::string error;
    const Fd state = open_state_dir(c, error);
    if (!state.ok()) {
        return refuse(error);
    }
    const IncomingGuard cleanup(state.get());
    remove_tree_at(state.get(), "incoming", error);
    if (::mkdirat(state.get(), "incoming", 0700) != 0) {
        return refuse(std::string("incoming: ") + errno_text(errno));
    }
    const Fd incoming = open_directory_at(state.get(), "incoming", error);
    if (!incoming.ok()) {
        return refuse(error);
    }
    if (!take_file(c, update_fd, incoming.get(), kReleaseFile, kMaxTarballBytes, error) ||
        !take_file(c, update_fd, incoming.get(), kSignatureFile, kMaxSignatureBytes, error)) {
        return refuse(error);
    }
    if (consume_interrupt()) {
        return refuse("the update was interrupted while its files were copied");
    }
    MappedFile tar;
    TarResult listed;
    if (const StageResult checked = check_and_list(incoming.get(), version, running, tar, listed);
        !checked.ok) {
        return refuse(checked.error);
    }
    Fd versions = open_directory(c.versions_dir(), error);
    if (!versions.ok()) {
        return refuse(error);
    }
    remove_tree_at(versions.get(), version + ".new", error);
    remove_tree_at(versions.get(), version, error);
    if (consume_interrupt()) {
        return refuse("the update was interrupted before it was unpacked");
    }
    const std::string fresh = c.version_dir(version) + ".new";
    const UnpackResult written = unpack_tar(listed.archive, tar.bytes(), fresh);
    if (!written.ok) {
        discard_version(c, version);
        return refuse(written.reason);
    }
    ::sync(); // the files are on disk before anything can point at them
    if (consume_interrupt()) {
        discard_version(c, version);
        return refuse("the update was interrupted after it was unpacked");
    }
    if (const std::string why = tree_problem(fresh); !why.empty()) {
        discard_version(c, version);
        return refuse("the unpacked folder was refused: " + why);
    }
    if (::rename(fresh.c_str(), c.version_dir(version).c_str()) != 0) {
        const std::string why = errno_text(errno);
        discard_version(c, version);
        return refuse("the unpacked folder could not be put in place: " + why);
    }
    sync_directory(versions.get());
    return {true, {}};
}

} // namespace etude::helper
