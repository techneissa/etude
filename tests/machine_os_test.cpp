// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/machine_os.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>

using namespace etude;

TEST_CASE("machine os: Raspberry Pi OS's os-release") {
    MachineOsText m;
    parse_os_release("PRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\nNAME=\"Debian GNU/Linux\"\n"
                     "VERSION_ID=\"13\"\nVERSION=\"13 (trixie)\"\nID=debian\n",
                     m);
    CHECK(m.id == "debian");
    CHECK(m.version_id == "13");
    CHECK(m.id_like.empty());
}

TEST_CASE("machine os: Ubuntu names Debian only as what it is like") {
    MachineOsText m;
    parse_os_release("# a comment\nID=ubuntu\nID_LIKE=debian\nVERSION_ID='24.04'\r\n", m);
    CHECK(m.id == "ubuntu");
    CHECK(m.id_like == "debian");
    CHECK(m.version_id == "24.04");
}

TEST_CASE("machine os: a missing key stays empty and the last of a repeated key wins") {
    MachineOsText m;
    parse_os_release("", m);
    CHECK(m.id.empty());
    parse_os_release("ID=a\nID=b\nnonsense\n=x\n", m);
    CHECK(m.id == "b");
}

TEST_CASE("machine os: dpkg's names for the kernel's") {
    CHECK(debian_arch("aarch64") == "arm64");
    CHECK(debian_arch("x86_64") == "amd64");
    CHECK(debian_arch("armv7l") == "armhf");
    CHECK(debian_arch("riscv64") == "riscv64");
}

TEST_CASE("machine os: the architecture is the build's, so a 32-bit userland on a 64-bit kernel refuses arm64 releases") {
#if defined(__aarch64__)
    CHECK(build_debian_arch() == "arm64");
#elif defined(__x86_64__)
    CHECK(build_debian_arch() == "amd64");
#elif defined(__arm__)
    CHECK(build_debian_arch() == "armhf");
#else
    CHECK(build_debian_arch().empty());
#endif
    // The userland says armhf whatever the kernel says: an arm64 release does not fit, an armhf one does.
    MachineOsText userland;
    userland.arch = "armhf";
    userland.id = "debian";
    userland.version_id = "13";
    userland.debian_version = "13.1";
    ReleaseInfo arm64;
    arm64.arch = "arm64";
    arm64.min_os = "12";
    CHECK_FALSE(release_fits_machine(arm64, userland.view()));
    arm64.arch = "armhf";
    CHECK(release_fits_machine(arm64, userland.view()));
    // Nothing reads the kernel's machine name for it any more.
    for (const char* file : {"/app/update_service.cpp", "/helper/install.cpp"}) {
        std::ifstream in(std::string(ETUDE_SOURCE_DIR) + file);
        REQUIRE(in.good());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CAPTURE(file);
        CHECK(text.find("uname(") == std::string::npos);
    }
}

TEST_CASE("machine os: the first line of /etc/debian_version") {
    CHECK(first_line("13.1\n") == "13.1");
    CHECK(first_line("  trixie/sid \n more") == "trixie/sid");
    CHECK(first_line("").empty());
    MachineOsText m;
    m.arch = "arm64";
    m.id = "debian";
    CHECK(m.view().arch == "arm64");
    CHECK(m.view().id == "debian");
}

TEST_CASE("machine os: a release signed for a Debian release fits that release or a newer one") {
    MachineOsText pi;
    pi.arch = "arm64";
    parse_os_release("ID=debian\nVERSION_ID=\"13\"\n", pi);
    CHECK(signed_os_fits_machine("trixie", pi.view()));
    CHECK(signed_os_fits_machine("bookworm", pi.view())); // built on an older one: it runs here
    CHECK_FALSE(signed_os_fits_machine("forky", pi.view())); // built on a newer one
    CHECK_FALSE(signed_os_fits_machine("sid", pi.view()));   // a name that is not known
    CHECK_FALSE(signed_os_fits_machine("", pi.view()));
    MachineOsText old;
    old.arch = "arm64";
    parse_os_release("ID=raspbian\nVERSION_ID=\"12\"\n", old);
    CHECK(signed_os_fits_machine("bookworm", old.view()));
    CHECK_FALSE(signed_os_fits_machine("trixie", old.view()));
    MachineOsText other;
    other.arch = "arm64";
    parse_os_release("ID=fedora\nVERSION_ID=\"40\"\n", other);
    CHECK_FALSE(signed_os_fits_machine("trixie", other.view()));
    MachineOsText nothing; // no os-release at all
    CHECK_FALSE(signed_os_fits_machine("trixie", nothing.view()));
}

TEST_CASE("machine os: a release signed for macos fits a Mac and nothing else") {
    MachineOsText mac;
    mac.arch = "arm64";
    mac.id = "macos";
    mac.version_id = "14.5";
    CHECK(signed_os_fits_machine("macos", mac.view()));
    CHECK_FALSE(signed_os_fits_machine("trixie", mac.view())); // a Debian release is not a Mac's
    MachineOsText pi;
    pi.arch = "arm64";
    parse_os_release("ID=debian\nVERSION_ID=\"13\"\n", pi);
    CHECK_FALSE(signed_os_fits_machine("macos", pi.view()));
    CHECK_FALSE(signed_os_fits_machine("macos", MachineOsText().view()));
}
