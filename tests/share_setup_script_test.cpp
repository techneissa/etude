// SPDX-License-Identifier: GPL-3.0-or-later
// The network folder's part of deploy/pi/setup.sh (step 7, --share-only), run against a folder that
// stands in for the Pi's /etc and home: no Samba, no root. What it writes is read back here: the
// share's file, the include line, the private interfaces only, no guest, one user; and a second run
// changes nothing.
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include "etude/network_share.hpp"

using namespace upload_test;

namespace {

const std::string kScript = std::string(ETUDE_SOURCE_DIR) + "/deploy/pi/setup.sh";

// Runs the script with bash; `args` and `env` are shell words.
ShellRun setup(const fs::path& sandbox, const std::string& args, const std::string& env = "") {
    return shell(env + " bash '" + kScript + "' --share-sandbox '" + sandbox.string() + "' " + args);
}

int count(const std::string& text, const std::string& word) {
    int n = 0;
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
        ++n;
    }
    return n;
}

bool has_line(const std::string& text, const std::string& line) {
    return ("\n" + text).find("\n" + line + "\n") != std::string::npos;
}

} // namespace

TEST_CASE("share setup: the share's file, bound to the private interfaces, no guest") {
    TempDir pi;
    const ShellRun run = setup(pi.path, "--share-interface eth0 --share-interface wlan0",
                          "ETUDE_SHARE_PASSWORD=Secret123");
    INFO(run.out);
    REQUIRE(run.status == 0);
    const std::string conf = read_file(pi.path / "etc/samba/etude-share.conf");
    CHECK(has_line(conf, "   interfaces = lo eth0 wlan0"));
    CHECK(has_line(conf, "   bind interfaces only = yes"));
    CHECK(has_line(conf, "   hosts deny = ALL"));
    CHECK(conf.find("hosts allow = 127.0.0.1 10.0.0.0/8 172.16.0.0/12 192.168.0.0/16") !=
          std::string::npos);
    CHECK(has_line(conf, "   map to guest = Never"));
    CHECK(has_line(conf, "   guest ok = no"));
    CHECK(has_line(conf, "   valid users = etude"));
    CHECK(has_line(conf, "   read only = no"));
    CHECK(has_line(conf, "[etude]"));
    CHECK(has_line(conf, "   vfs objects = catia fruit streams_xattr")); // ? : * " < > | mapped
    CHECK(conf.find("guest ok = yes") == std::string::npos);
    // The share is the Inbox of the app user's Songs folder, made for the app.
    const std::string inbox =
        (pi.path / "home/etude/.local/share/io.github.techneissa.etude/Songs/Inbox").string();
    CHECK(has_line(conf, "   path = " + inbox));
    CHECK(fs::is_directory(inbox));
    // smb.conf includes it once, and a copy of the Pi's own file is kept.
    const std::string smb = read_file(pi.path / "etc/samba/smb.conf");
    CHECK(count(smb, "include = " + (pi.path / "etc/samba/etude-share.conf").string()) == 1);
    CHECK(fs::exists(pi.path / "etc/samba/smb.conf.before-etude"));
    CHECK(read_file(pi.path / "etc/samba/smb.conf.before-etude").find("include") ==
          std::string::npos);
    // A password that was given is never printed; the app reads the share back from these files.
    CHECK(run.out.find("Secret123") == std::string::npos);
    CHECK(run.out.find("with the password you gave") != std::string::npos);
    CHECK(run.out.find(etude::share_windows_address()) != std::string::npos);
    CHECK(run.out.find(etude::share_smb_address()) != std::string::npos);
    CHECK(etude::smb_conf_has_share(conf, etude::kShareName));
}

TEST_CASE("share setup: running it again changes nothing and keeps the password") {
    TempDir pi;
    REQUIRE(setup(pi.path, "--share-interface eth0", "ETUDE_SHARE_PASSWORD=Secret123").status ==
            0);
    const std::string conf = read_file(pi.path / "etc/samba/etude-share.conf");
    const std::string smb = read_file(pi.path / "etc/samba/smb.conf");
    const ShellRun again = setup(pi.path, "--share-interface eth0");
    INFO(again.out);
    REQUIRE(again.status == 0);
    CHECK(count(again.out, "[change]") == 0);
    CHECK(again.out.find("already has a network password") != std::string::npos);
    CHECK(again.out.find("made for you") == std::string::npos);
    CHECK(read_file(pi.path / "etc/samba/etude-share.conf") == conf);
    CHECK(read_file(pi.path / "etc/samba/smb.conf") == smb);
}

TEST_CASE("share setup: a changed interface rewrites the file, the include stays single") {
    TempDir pi;
    REQUIRE(setup(pi.path, "--share-interface eth0", "ETUDE_SHARE_PASSWORD=x").status == 0);
    const ShellRun run = setup(pi.path, "--share-interface wlan0");
    REQUIRE(run.status == 0);
    CHECK(
        has_line(read_file(pi.path / "etc/samba/etude-share.conf"), "   interfaces = lo wlan0"));
    CHECK(count(read_file(pi.path / "etc/samba/smb.conf"), "include = ") == 1);
}

TEST_CASE("share setup: --reset-share-password sets a new password") {
    TempDir pi;
    REQUIRE(setup(pi.path, "--share-interface eth0", "ETUDE_SHARE_PASSWORD=one").status == 0);
    const ShellRun run = setup(pi.path, "--share-interface eth0 --reset-share-password",
                          "ETUDE_SHARE_PASSWORD=zq7Wd9Kx");
    REQUIRE(run.status == 0);
    CHECK(run.out.find("zq7Wd9Kx") == std::string::npos); // given, so not printed
    CHECK(run.out.find("with the password you gave") != std::string::npos);
}

TEST_CASE("share setup: with no password given one is made and printed once") {
    TempDir pi;
    const ShellRun run = setup(pi.path, "--share-interface eth0");
    REQUIRE(run.status == 0);
    const std::size_t at = run.out.find("with the password ");
    REQUIRE(at != std::string::npos);
    const std::string rest = run.out.substr(at + 18);
    CHECK(rest.find(' ') == 12); // twelve characters
    CHECK(rest.find("(made for you") != std::string::npos);
    // It is printed once, and the second run, which keeps it, does not print it.
    const std::string word = rest.substr(0, 12);
    const ShellRun again = setup(pi.path, "--share-interface eth0");
    CHECK(again.out.find(word) == std::string::npos);
}

TEST_CASE("share setup: the share can be a folder of the caller's choosing") {
    TempDir pi;
    const fs::path folder = pi.path / "music";
    const ShellRun run = setup(pi.path, "--share-interface eth0 --share-path '" + folder.string() + "'");
    REQUIRE(run.status == 0);
    CHECK(has_line(read_file(pi.path / "etc/samba/etude-share.conf"),
                   "   path = " + folder.string()));
    CHECK(fs::is_directory(folder));
}

TEST_CASE("share setup: only home-network interfaces are shared on, not VPNs or virtual ones") {
    TempDir pi;
    write_file(pi.path / "ip.txt",
               "2: eth0    inet 192.168.1.5/24 brd 192.168.1.255 scope global eth0\n"
               "3: wlan0    inet 10.0.0.7/8 brd 10.255.255.255 scope global wlan0\n"
               "4: tailscale0    inet 100.64.1.1/32 scope global tailscale0\n"
               "5: tun0    inet 10.8.0.2/24 scope global tun0\n"
               "6: wg0    inet 172.20.0.2/24 scope global wg0\n"
               "7: docker0    inet 172.17.0.1/16 scope global docker0\n"
               "8: eth1    inet 172.32.0.1/24 scope global eth1\n"
               "9: eth2    inet 172.16.5.5/16 scope global eth2\n"
               "10: usb0    inet 169.254.1.1/16 scope global usb0\n"
               "11: utun3    inet 10.9.0.1/24 scope global utun3\n"
               "12: veth1@if5    inet 192.168.9.1/24 scope global veth1\n"
               "13: eth3    inet 8.8.4.4/24 scope global eth3\n");
    const ShellRun run =
        setup(pi.path, "", "ETUDE_SHARE_IP_LISTING='" + (pi.path / "ip.txt").string() + "'");
    INFO(run.out);
    REQUIRE(run.status == 0);
    // eth0 and wlan0 (192.168 and 10), eth2 (172.16/12); not 172.32, link-local, a public address,
    // nor any tunnel, VPN, container or virtual interface.
    CHECK(has_line(read_file(pi.path / "etc/samba/etude-share.conf"),
                   "   interfaces = lo eth0 eth2 wlan0"));
}

TEST_CASE("share setup: a share that testparm refuses is not installed") {
    TempDir pi;
    fs::create_directories(pi.path / "bin");
    write_script(pi.path / "bin/testparm", "#!/bin/sh\necho 'Unknown parameter' >&2\nexit 1\n");
    const ShellRun run =
        setup(pi.path, "--share-interface eth0", "PATH='" + (pi.path / "bin").string() + "':$PATH");
    INFO(run.out);
    CHECK(run.status != 0);
    CHECK(run.out.find("testparm refused the share") != std::string::npos);
    CHECK_FALSE(fs::exists(pi.path / "etc/samba/etude-share.conf"));
    CHECK(read_file(pi.path / "etc/samba/smb.conf").find("include") == std::string::npos);
    // The same with a testparm that agrees: installed.
    write_script(pi.path / "bin/testparm", "#!/bin/sh\nexit 0\n");
    REQUIRE(
        setup(pi.path, "--share-interface eth0", "PATH='" + (pi.path / "bin").string() + "':$PATH")
            .status == 0);
    CHECK(fs::exists(pi.path / "etc/samba/etude-share.conf"));
}

TEST_CASE("share setup: the dry run prints the plan") {
    const ShellRun dry = shell("bash '" + kScript + "' --dry-run --share-only --share-interface eth0");
    const std::string& out = dry.out;
    CHECK(dry.status == 0);
    CHECK(out.find("Network folder") != std::string::npos);
    CHECK(out.find("etude-share.conf") != std::string::npos);
    CHECK(out.find("+ cp ") != std::string::npos);              // printed ...
    CHECK(out.find("(sandbox, not run)") == std::string::npos); // ... not the sandbox's way
}

TEST_CASE("share setup: the command the app shows is a real option of the script") {
    const std::string script = read_file(kScript);
    const std::string command = etude::kShareSetupCommand;
    CHECK(command == "sudo deploy/pi/setup.sh --share-only");
    CHECK(script.find("--share-only)") != std::string::npos);
}
