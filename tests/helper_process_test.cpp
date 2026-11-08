// SPDX-License-Identifier: GPL-3.0-or-later
// How the helper runs systemctl and runs its own code as the app's user, and the state it keeps.
#include "../helper/interrupt.hpp"
#include "../helper/process.hpp"
#include "../helper/safe_io.hpp"
#include "../helper/service.hpp"
#include "../helper/state_store.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <signal.h>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace etude;
using namespace etude::helper;
using namespace upload_test;

namespace {

AppUser me(const fs::path& home) {
    const passwd* entry = ::getpwuid(::geteuid());
    REQUIRE(entry != nullptr);
    AppUser user;
    user.name = entry->pw_name;
    user.uid = entry->pw_uid;
    user.gid = entry->pw_gid;
    user.home = home.string();
    return user;
}

Context context(const fs::path& root) {
    Context c;
    c.conf.prefix = (root / "opt").string();
    c.conf.state_dir = (root / "state").string();
    c.user = me(root / "home");
    return c;
}

// A systemctl that keeps the running unit in a file and logs its calls. Files in its folder change
// what it says: `answer` is what is-active prints for every unit (and exits 3), `bus_error` makes
// is-active say nothing and fail, `hang` makes stop outlast the helper's patience.
fs::path fake_systemctl(const fs::path& root, const std::string& environment = "") {
    const fs::path script = root / "systemctl";
    write_file(script,
               "#!/bin/sh\n"
               "dir=\"" + root.string() + "\"\n"
               "echo \"$*\" >>\"$dir/calls\"\n"
               "case \"$1\" in\n"
               "is-active) if [ -e \"$dir/bus_error\" ]; then echo 'Failed to connect to bus' >&2; exit 1;\n"
               "  elif [ -e \"$dir/answer\" ]; then cat \"$dir/answer\"; exit 3;\n"
               "  elif [ \"$(cat \"$dir/active\" 2>/dev/null)\" = \"$2\" ]; then echo active;\n"
               "  else echo inactive; exit 3; fi ;;\n"
               "stop) [ -e \"$dir/hang\" ] && sleep 5; [ -e \"$dir/stuck\" ] || rm -f \"$dir/active\" ;;\n"
               "start|restart) echo \"$2\" >\"$dir/active\" ;;\n"
               "show) printf '%s\\n' '" + environment + "' ;;\n"
               "esac\n");
    ::chmod(script.c_str(), 0755);
    return script;
}

} // namespace

TEST_CASE("process: a program runs with no shell, its output is kept and a slow one is killed") {
    const ProgramResult echoed = run_program({"/bin/echo", "a;b", "$HOME"}, 10);
    CHECK(echoed.ran);
    CHECK(echoed.exit_code == 0);
    CHECK(echoed.output == "a;b $HOME\n");
    CHECK(run_program({"/usr/bin/false"}, 10).exit_code == 1);
    const ProgramResult slow = run_program({"/bin/sleep", "30"}, 1);
    CHECK(slow.timed_out);
    CHECK(slow.exit_code == -1);
    CHECK_FALSE(run_program({"sleep", "1"}, 1).ran); // a program is named by its full path
    CHECK(run_program({"/nonexistent/program"}, 5).exit_code == 127);
}

TEST_CASE("process: the app's user is looked up, and root is refused") {
    AppUser user;
    std::string error;
    CHECK_FALSE(find_app_user("root", user, error));
    CHECK(error.find("root") != std::string::npos);
    CHECK_FALSE(find_app_user("no-such-user-here", user, error));
    const passwd* entry = ::getpwuid(::geteuid());
    if (::geteuid() != 0) {
        CHECK(find_app_user(entry->pw_name, user, error));
        CHECK(user.uid == ::geteuid());
    }
}

TEST_CASE("process: code runs in a child, and what it says comes back") {
    TempDir t;
    const AppUser user = me(t.path);
    std::string error;
    CHECK(run_as_user(user, 10, [&] {
        write_file(t.path / "child", "written by the child");
        return std::string();
    }, error));
    CHECK(read_file(t.path / "child") == "written by the child");
    CHECK_FALSE(run_as_user(user, 10, [] { return std::string("no space left"); }, error));
    CHECK(error == "no space left");
    CHECK_FALSE(run_as_user(user, 10, []() -> std::string { throw std::runtime_error("it threw"); }, error));
    CHECK(error == "it threw");
    // A child that crashes or never ends is reported, and the helper goes on.
    CHECK_FALSE(run_as_user(user, 10, []() -> std::string { ::_exit(9); }, error));
    CHECK_FALSE(run_as_user(user, 1, []() -> std::string { ::sleep(30); return {}; }, error));
}

TEST_CASE("process: the app's user's child starts with no environment and no inherited descriptors") {
    TempDir t;
    const AppUser user = me(t.path);
    ::setenv("ETUDE_HELPER_SECRET", "from the helper's parent", 1);
    const int leaked = ::open("/dev/null", O_RDONLY); // no close-on-exec: a fork would hand it down
    REQUIRE(leaked > 2);
    std::string error;
    CHECK(run_as_user(user, 10, [leaked] {
        if (::getenv("ETUDE_HELPER_SECRET") != nullptr) {
            return std::string("the environment was inherited");
        }
        if (::getenv("PATH") != nullptr || ::getenv("HOME") != nullptr) {
            return std::string("the environment is not empty");
        }
        if (::fcntl(leaked, F_GETFD) != -1) {
            return std::string("a descriptor was inherited");
        }
        return std::string();
    }, error));
    CHECK(error.empty());
    ::close(leaked);
    ::unsetenv("ETUDE_HELPER_SECRET");
}

TEST_CASE("process: a stop signal ends a wait, unless the call is part of the clean-up") {
    TempDir t;
    const AppUser user = me(t.path);
    reset_interrupt();
    raise_interrupt();
    const ProgramResult cut = run_program({"/bin/sleep", "30"}, 30);
    CHECK(cut.interrupted);
    CHECK_FALSE(cut.timed_out);
    // The signal was acted on once; a clean-up call afterwards runs whole.
    const ProgramResult cleanup = run_program({"/bin/echo", "done"}, 10, false);
    CHECK_FALSE(cleanup.interrupted);
    CHECK(cleanup.output == "done\n");
    // The signal reaches the next checkpoint, which is where the helper acts on it ...
    CHECK(consume_interrupt());
    CHECK_FALSE(consume_interrupt()); // ... once.
    CHECK(was_interrupted());
    reset_interrupt();
    // The same for the child that runs as the app's user.
    raise_interrupt();
    std::string error;
    CHECK_FALSE(run_as_user(user, 30, []() -> std::string { ::sleep(30); return {}; }, error));
    CHECK(error == "interrupted");
    reset_interrupt();
    // A call that is not interruptible ignores a signal that is already there.
    raise_interrupt();
    CHECK(run_program({"/bin/echo", "x"}, 10, false).exit_code == 0);
    reset_interrupt();
    // And a real signal is caught, not fatal.
    install_interrupt_handlers();
    ::raise(SIGTERM);
    CHECK(consume_interrupt());
    ::raise(SIGINT);
    CHECK(consume_interrupt());
    reset_interrupt();
}

TEST_CASE("service: stop is checked, and a service that will not stop is reported") {
    TempDir t;
    Context c = context(t.path);
    c.conf.systemctl = fake_systemctl(t.path).string();
    const Service service(c);
    CHECK(service.active_unit().empty());
    CHECK(service.start("etude.service"));
    CHECK(service.state("etude.service") == "active");
    CHECK(service.running("etude.service"));
    CHECK(service.active_unit() == "etude.service");
    std::string error;
    CHECK(service.stop("etude.service", error));
    CHECK(service.state("etude.service") == "inactive");
    CHECK_FALSE(service.running("etude.service"));
    CHECK(service.start("etude-labwc.service"));
    CHECK(service.active_unit() == "etude-labwc.service");
    write_file(t.path / "stuck", "");
    CHECK_FALSE(service.stop("etude-labwc.service", error));
    CHECK(error.find("is active after it was told to stop") != std::string::npos);
    // Only the app's two services can be named.
    CHECK_FALSE(service.start("sshd.service"));
    CHECK_FALSE(service.stop("sshd.service", error));
    CHECK(service.running("sshd.service")); // not ours: no answer, so "running"
    CHECK(service.state("sshd.service").empty());
}

TEST_CASE("service: only inactive and failed count as stopped; every other answer is running") {
    TempDir t;
    Context c = context(t.path);
    c.conf.systemctl = fake_systemctl(t.path).string();
    // The default time limits: this test is about the words, and a tight limit made a slow start of the
    // fake's shell look like a timed-out call.
    const Service service(c);
    for (const char* word : {"inactive", "failed"}) {
        CAPTURE(word);
        write_file(t.path / "answer", std::string(word) + "\n");
        CHECK_FALSE(service.running("etude.service"));
        write_file(t.path / "active", "etude.service\n"); // a stop that leaves it "active" file-wise
        std::string error;
        CHECK(service.stop("etude.service", error)); // the word after the stop is what counts
    }
    for (const char* word : {"activating", "deactivating", "active", "reloading", "refreshing",
                             "maintenance", "something-new"}) {
        CAPTURE(word);
        write_file(t.path / "answer", std::string(word) + "\n");
        CHECK(service.running("etude.service"));
        CHECK(service.active_unit() == "etude.service");
        std::string error;
        CHECK_FALSE(service.stop("etude.service", error));
        CHECK(error.find(word) != std::string::npos);
    }
    // More than one word, or none, is no answer, and no answer is not "stopped".
    write_file(t.path / "answer", "inactive\nactive\n");
    CHECK(service.running("etude.service"));
    write_file(t.path / "answer", "\n");
    CHECK(service.running("etude.service"));
    fs::remove(t.path / "answer");
}

TEST_CASE("service: a bus that does not answer, or a stop that hangs, fails closed") {
    TempDir t;
    Context c = context(t.path);
    c.conf.systemctl = fake_systemctl(t.path).string();
    c.service_timeout_s = 2; // under the fake's 5 s hang below; the bus error answers at once
    const Service service(c);
    write_file(t.path / "bus_error", "");
    CHECK(service.state("etude.service").empty());
    CHECK(service.running("etude.service"));
    CHECK(service.active_unit() == "etude.service");
    std::string error;
    CHECK_FALSE(service.stop("etude.service", error));
    CHECK(error.find("could not be read") != std::string::npos);
    fs::remove(t.path / "bus_error");
    // A stop that outlasts the helper's patience: the call is killed, and the state afterwards decides.
    write_file(t.path / "hang", "");
    write_file(t.path / "active", "etude.service\n");
    CHECK_FALSE(service.stop("etude.service", error));
    // Either the unit is seen still active, or (a start of the shell slower than the limit) the answer
    // after the stop did not come in time: both fail closed.
    CHECK((error.find("is active") != std::string::npos ||
           error.find("could not be read") != std::string::npos));
}

TEST_CASE("service: the data folder is where SDL puts it, with XDG_DATA_HOME from the service alone") {
    TempDir t;
    Context c = context(t.path);
    c.conf.systemctl = fake_systemctl(t.path, "SDL_VIDEO_DRIVER=kmsdrm XDG_DATA_HOME=/srv/share").string();
    {
        const Service service(c);
        CHECK(data_folder_for(c, service, "etude.service") == "/srv/share/io.github.techneissa.etude");
        CHECK(data_folder_for(c, service, "") == "/srv/share/io.github.techneissa.etude"); // the main service's
    }
    c.conf.systemctl = fake_systemctl(t.path, "SDL_VIDEO_DRIVER=kmsdrm").string();
    {
        const Service service(c);
        CHECK(data_folder_for(c, service, "etude.service") ==
              (t.path / "home" / ".local/share/io.github.techneissa.etude").string());
    }
    // An environment that cannot be read is no answer: the home folder is not quietly used instead.
    for (const char* env : {"\"XDG_DATA_HOME=/a b\" X=1", "XDG_DATA_HOME=relative", "XDG_DATA_HOME=",
                            "\"XDG_DATA_HOME=/a"}) {
        CAPTURE(env);
        c.conf.systemctl = fake_systemctl(t.path, env).string();
        const Service service(c);
        CHECK(data_folder_for(c, service, "etude.service").empty());
    }
    // systemctl failing to answer `show` is no answer either.
    write_file(t.path / "systemctl", "#!/bin/sh\nexit 1\n");
    ::chmod((t.path / "systemctl").c_str(), 0755);
    c.conf.systemctl = (t.path / "systemctl").string();
    const Service broken(c);
    CHECK(data_folder_for(c, broken, "etude.service").empty());
}

TEST_CASE("service: a data folder reached through a link the app made is refused") {
    TempDir t;
    Context c = context(t.path);
    c.conf.systemctl = fake_systemctl(t.path, "").string();
    const Service service(c);
    const fs::path home = t.path / "home";
    fs::create_directories(t.path / "elsewhere");
    fs::create_directories(home / ".local/share");
    // The app's user makes its data folder a link: ReadWritePaths would follow it.
    const fs::path folder = home / ".local/share" / kAppDataFolderName;
    fs::create_directory_symlink(t.path / "elsewhere", folder);
    if (::geteuid() == 0) {
        // ci-linux runs the tests as root, whose own links are allowed: make this one the app's.
        REQUIRE(::lchown(folder.c_str(), 65534, 65534) == 0);
    }
    CHECK(data_folder_for(c, service, "etude.service").empty());
    fs::remove(folder);
    fs::create_directories(folder);
    CHECK(data_folder_for(c, service, "etude.service") == folder.string());
}

TEST_CASE("state store: the state is kept whole in a folder only the helper can enter") {
    TempDir t;
    const Context c = context(t.path);
    std::string error;
    CHECK(load_pending(c, error).phase == UpdatePhase::None);
    PendingUpdate s;
    s.phase = UpdatePhase::Pending;
    s.from = "1.0.0";
    s.to = "1.1.0";
    s.starts = 1;
    CHECK(save_pending(c, s, error));
    CHECK((fs::status(t.path / "state").permissions() & (fs::perms::group_all | fs::perms::others_all)) ==
          fs::perms::none);
    const PendingUpdate back = load_pending(c, error);
    CHECK(back.phase == UpdatePhase::Pending);
    CHECK(back.to == "1.1.0");
    CHECK(back.starts == 1);
    CHECK(tree(t.path / "state") == "state\n");
}

TEST_CASE("state store: a state file that cannot be read is set aside, and counts as none") {
    TempDir t;
    const Context c = context(t.path);
    std::string error;
    write_file(t.path / "state" / "state", "phase=pending\nfrom=banana\n");
    ::chmod((t.path / "state").c_str(), 0700);
    CHECK(load_pending(c, error).phase == UpdatePhase::None);
    CHECK_FALSE(error.empty());
    CHECK(fs::exists(t.path / "state" / "state.unreadable"));
    CHECK_FALSE(fs::exists(t.path / "state" / "state"));
}

TEST_CASE("state store: a state folder that others can enter or that is a link is refused") {
    TempDir t;
    const Context c = context(t.path);
    fs::create_directories(t.path / "state");
    ::chmod((t.path / "state").c_str(), 0755);
    std::string error;
    CHECK_FALSE(open_state_dir(c, error).ok());
    CHECK(error.find("root's alone") != std::string::npos);
    fs::remove(t.path / "state");
    fs::create_directories(t.path / "elsewhere");
    ::chmod((t.path / "elsewhere").c_str(), 0700);
    fs::create_symlink(t.path / "elsewhere", t.path / "state");
    CHECK_FALSE(open_state_dir(c, error).ok());
}

TEST_CASE("state store: two runs cannot hold the same lock") {
    TempDir t;
    const Context c = context(t.path);
    std::string error;
    StateLock first(c, "run.lock", false, error);
    REQUIRE(first.held());
    StateLock second(c, "run.lock", false, error);
    CHECK_FALSE(second.held());
    StateLock other(c, "lock", false, error); // another lock is another lock
    CHECK(other.held());
}
