// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/network_share.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("network share: the words a computer types") {
    CHECK(share_windows_address() == "\\\\etude.local\\etude");
    CHECK(share_smb_address() == "smb://etude.local/etude");
}

TEST_CASE("network share: a smb.conf with the share") {
    const char* conf =
        "[global]\n   workgroup = WORKGROUP\n\n[printers]\n   path = /var/tmp\n\n[etude]\n"
        "   path = /home/etude/Songs/Inbox\n   read only = no\n";
    CHECK(smb_conf_has_share(conf, "etude"));
    CHECK(smb_conf_has_share(conf, "ETUDE")); // share names are not case sensitive
    CHECK_FALSE(smb_conf_has_share(conf, "music"));
}

TEST_CASE("network share: comments, spaces and a share that is switched off") {
    CHECK_FALSE(smb_conf_has_share("# [etude]\n; [etude]\n[global]\n", "etude"));
    CHECK(smb_conf_has_share("  [ etude ]  \npath = /x\n", "etude"));
    CHECK_FALSE(smb_conf_has_share("[etude]\n   available = no\n", "etude"));
    CHECK(smb_conf_has_share("[etude]\n   available = yes\n", "etude"));
    // "available = no" of another share does not switch this one off.
    CHECK(smb_conf_has_share("[other]\n available = no\n[etude]\n path = /x\n", "etude"));
    CHECK_FALSE(smb_conf_has_share("", "etude"));
    CHECK_FALSE(smb_conf_has_share("[etude-old]\n", "etude"));
}

TEST_CASE("network share: the files a smb.conf includes") {
    const auto files =
        smb_conf_includes("[global]\n include = /etc/samba/etude-share.conf\n; include = /no\n"
                          "include = /etc/samba/%U.conf\nInclude=/etc/samba/b.conf\n");
    REQUIRE(files.size() == 2);
    CHECK(files[0] == "/etc/samba/etude-share.conf");
    CHECK(files[1] == "/etc/samba/b.conf");
}
