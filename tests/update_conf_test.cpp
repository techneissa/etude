// SPDX-License-Identifier: GPL-3.0-or-later
// The root helper's settings file is data, not shell: what it takes and what makes it refused.
#include "etude/update_conf.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("update conf: the keys setup.sh writes are read, quoted or not") {
    const ConfResult r = parse_update_conf("# written by setup.sh\nPREFIX=\"/opt/etude\"\nAPP_USER=etude\n\n");
    REQUIRE(r.ok());
    CHECK(r.conf.prefix == "/opt/etude");
    CHECK(r.conf.app_user == "etude");
    CHECK(r.conf.state_dir == "/var/lib/etude-update"); // the defaults stand for what is absent
    CHECK(r.conf.ready_timeout_s == 30);
    CHECK(r.conf.max_starts == 2);
    CHECK(parse_update_conf("").ok());
}

TEST_CASE("update conf: every key, with its limits") {
    const ConfResult r = parse_update_conf(
        "PREFIX=/srv/t\nAPP_USER=kiosk_1\nSTATE_DIR=/var/lib/x\nSYSTEMCTL=/bin/true\n"
        "SERVICE_TIMEOUT=7\nREADY_TIMEOUT=5\nMAX_STARTS=3\n");
    REQUIRE(r.ok());
    CHECK(r.conf.state_dir == "/var/lib/x");
    CHECK(r.conf.systemctl == "/bin/true");
    CHECK(r.conf.service_timeout_s == 7);
    CHECK(r.conf.ready_timeout_s == 5);
    CHECK(r.conf.max_starts == 3);
    CHECK(parse_update_conf("").conf.service_timeout_s == 90);
    // The limits are the ones the units' time limits are worked out for (helper/context.hpp).
    CHECK(parse_update_conf("SERVICE_TIMEOUT=90\nREADY_TIMEOUT=120\nMAX_STARTS=4\n").ok());
    CHECK(kMaxStartsLimit < 5); // StartLimitBurst of the app units
    for (const char* bad : {"SERVICE_TIMEOUT=1\n", "SERVICE_TIMEOUT=91\n", "SERVICE_TIMEOUT=300\n", "SERVICE_TIMEOUT=x\n",
                            "READY_TIMEOUT=0\n", "READY_TIMEOUT=121\n", "READY_TIMEOUT=x\n",
                            "READY_TIMEOUT=5s\n", "MAX_STARTS=0\n", "MAX_STARTS=5\n", "MAX_STARTS=6\n"}) {
        CAPTURE(bad);
        CHECK(parse_update_conf(bad).error == ConfError::BadValue);
    }
}

TEST_CASE("update conf: anything that could be shell is refused, not interpreted") {
    for (const char* bad :
         {"PREFIX=/opt/a b\n", "PREFIX=/opt/a;rm -rf /\n", "PREFIX=$(id)\n", "PREFIX=`id`\n",
          "PREFIX=/opt/$HOME\n", "PREFIX=\"/opt/x\"y\n", "PREFIX=\"/opt/x\n", "PREFIX=/opt/x\\\n",
          "PREFIX=/opt/a'b\n", "PREFIX=/opt/\"a\"\n", "PREFIX=opt/etude\n", "PREFIX=\n",
          "PREFIX=/opt/../etc\n", "PREFIX=/opt//etude\n", "PREFIX=/opt/etude/\n",
          "PREFIX=/opt/./etude\n", "APP_USER=Root\n", "APP_USER=a b\n", "APP_USER=\n",
          "APP_USER=-x\n", "APP_USER=root;id\n", "SYSTEMCTL=systemctl\n", "SYSTEMCTL=/\n",
          "SYSTEMCTL=/\n"}) {
        CAPTURE(bad);
        const ConfResult r = parse_update_conf(bad);
        CHECK_FALSE(r.ok());
        CHECK(r.error == ConfError::BadValue);
    }
}

TEST_CASE("update conf: unknown keys, doubled keys and lines that are not settings refuse the file") {
    CHECK(parse_update_conf("FOO=bar\n").error == ConfError::UnknownKey);
    CHECK(parse_update_conf("PREFIX=/a\nPREFIX=/b\n").error == ConfError::Duplicate);
    CHECK(parse_update_conf("export PREFIX=/a\n").error == ConfError::UnknownKey);
    CHECK(parse_update_conf("PREFIX /a\n").error == ConfError::BadLine);
    CHECK(parse_update_conf("=x\n").error == ConfError::BadLine);
    CHECK(parse_update_conf("if true; then\n").error == ConfError::BadLine);
    CHECK(parse_update_conf(std::string(5000, '#')).error == ConfError::TooLarge);
    const ConfResult r = parse_update_conf("APP_USER=etude\nFOO=bar\n");
    CHECK(r.line == 2);
    CHECK(r.key == "FOO");
    CHECK(conf_error_text(r) == "line 2: FOO is not a setting of this file");
}

TEST_CASE("update conf: plain paths and plain user names") {
    CHECK(plain_absolute_path("/"));
    CHECK(plain_absolute_path("/opt/etude"));
    CHECK(plain_absolute_path("/home/u/.local/share/x-y_z+1@2:3,4"));
    CHECK_FALSE(plain_absolute_path(""));
    CHECK_FALSE(plain_absolute_path("/a b"));
    CHECK_FALSE(plain_absolute_path("/a\n"));
    CHECK_FALSE(plain_absolute_path("/a/.."));
    CHECK_FALSE(plain_absolute_path(std::string("/") + std::string(200, 'a')));
    CHECK(plain_user_name("etude"));
    CHECK(plain_user_name("_k-1"));
    CHECK_FALSE(plain_user_name("1k"));
    CHECK_FALSE(plain_user_name(std::string(33, 'a')));
}
