// SPDX-License-Identifier: GPL-3.0-or-later
// Profiles: the names people type, the set of a household, the default every device has.
#include "etude/profile.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;

TEST_CASE("profile: the default profile is there without setting anything up") {
    const ProfileSet set;
    REQUIRE(set.size() == 1);
    CHECK(set.default_profile().id == kDefaultProfileId);
    CHECK(set.default_profile().name == "Player");
    CHECK(set.default_profile().is_default());
    CHECK(set.default_profile().colour == kProfileColours[0]);
    CHECK(set.resolve(77) == kDefaultProfileId);
    CHECK(set.resolve(kDefaultProfileId) == kDefaultProfileId);
}

TEST_CASE("profile: a name is trimmed, single-spaced, and cut at a character boundary") {
    CHECK(clean_profile_name("  Anna  ") == "Anna");
    CHECK(clean_profile_name("Anna\t \n Lee") == "Anna Lee");
    CHECK(clean_profile_name("An\x01na") == "An na");
    CHECK(clean_profile_name("   ").empty());
    CHECK(clean_profile_name("\xff\xfe").empty()); // not UTF-8
    // Cut to 32 bytes without splitting a two-byte letter (31 bytes + one é).
    const std::string long_name = std::string(31, 'a') + "\xc3\xa9" + "tail";
    const std::string cut = clean_profile_name(long_name);
    CHECK(cut == std::string(31, 'a'));
    CHECK(clean_profile_name(std::string(40, 'b')).size() == kMaxProfileNameBytes);
    CHECK(clean_profile_name(std::string(31, 'a') + " " + "tail") == std::string(31, 'a'));
}

TEST_CASE("profile: sanitized keeps a name and a 24-bit colour") {
    Profile p;
    p.name = "  ";
    p.colour = 0xFF123456;
    const Profile clean = sanitized(p);
    CHECK(clean.name == "Player");
    CHECK(clean.colour == 0x123456);
}

TEST_CASE("profile: adding gives the next id and the first unused colour") {
    ProfileSet set;
    Profile anna;
    REQUIRE(set.add("Anna", 1000, &anna) == ProfileResult::Ok);
    CHECK(anna.id == 2);
    CHECK(anna.colour == kProfileColours[1]); // the default profile has the first
    CHECK(anna.created_utc_s == 1000);
    Profile ben;
    REQUIRE(set.add("Ben", 1001, &ben) == ProfileResult::Ok);
    CHECK(ben.id == 3);
    CHECK(ben.colour == kProfileColours[2]);
    CHECK(set.size() == 3);
    CHECK(set.find(2)->name == "Anna");
    CHECK(set.find(9) == nullptr);
}

TEST_CASE("profile: an empty name, a name in use and a full household are refused") {
    ProfileSet set;
    CHECK(set.add("   ", 0) == ProfileResult::EmptyName);
    CHECK(set.add("player", 0) == ProfileResult::NameTaken); // the default's, in other letters
    REQUIRE(set.add("Anna", 0) == ProfileResult::Ok);
    CHECK(set.add("ANNA", 0) == ProfileResult::NameTaken);
    CHECK(set.size() == 2);
    while (set.size() < kMaxProfiles) {
        REQUIRE(set.add("P" + std::to_string(set.size()), 0) == ProfileResult::Ok);
    }
    CHECK(set.add("One more", 0) == ProfileResult::TooMany);
    CHECK(set.size() == kMaxProfiles);
}

TEST_CASE("profile: check_name answers what add and rename would, without changing anything") {
    ProfileSet set;
    REQUIRE(set.add("Anna", 0) == ProfileResult::Ok);
    REQUIRE(set.add("Ben", 0) == ProfileResult::Ok);
    const std::int64_t anna = set.all()[1].id;
    const std::size_t before = set.size();
    // Empty, and only blanks or control characters (they clean to nothing).
    CHECK(set.check_name("") == ProfileResult::EmptyName);
    CHECK(set.check_name("   \t ") == ProfileResult::EmptyName);
    CHECK(set.check_name("\x01\x02") == ProfileResult::EmptyName);
    // A name over the limit is cut, not refused: it is as good as its cut form.
    CHECK(set.check_name(std::string(kMaxProfileNameBytes + 10, 'x')) == ProfileResult::Ok);
    CHECK(set.check_name(std::string(kMaxProfileNameBytes + 10, 'x')) ==
          set.check_name(std::string(kMaxProfileNameBytes, 'x')));
    REQUIRE(set.add(std::string(kMaxProfileNameBytes, 'y'), 0) == ProfileResult::Ok);
    CHECK(set.check_name(std::string(kMaxProfileNameBytes + 5, 'y')) == ProfileResult::NameTaken);
    // A name in use, whatever its letter case or spacing; the default profile's too.
    CHECK(set.check_name("Anna") == ProfileResult::NameTaken);
    CHECK(set.check_name("anna") == ProfileResult::NameTaken);
    CHECK(set.check_name("  ANNA ") == ProfileResult::NameTaken);
    CHECK(set.check_name("player") == ProfileResult::NameTaken);
    CHECK(set.check_name("Annabel") == ProfileResult::Ok);
    // Editing a profile without renaming it (or only changing its case) stays valid; another
    // profile's name does not.
    CHECK(set.check_name("Anna", anna) == ProfileResult::Ok);
    CHECK(set.check_name("ANNA", anna) == ProfileResult::Ok);
    CHECK(set.check_name("Ben", anna) == ProfileResult::NameTaken);
    CHECK(set.check_name("", anna) == ProfileResult::EmptyName);
    CHECK(set.size() == before + 1); // nothing was added or renamed
}

TEST_CASE("profile: colours are used evenly once the palette is spent") {
    ProfileSet set;
    for (std::size_t i = 1; i < kProfileColours.size(); ++i) {
        REQUIRE(set.add("P" + std::to_string(i), 0) == ProfileResult::Ok);
    }
    // All eight are in use once; the next one takes the first of the least used.
    CHECK(set.unused_colour() == kProfileColours[0]);
    REQUIRE(set.add("Ninth", 0) == ProfileResult::Ok);
    CHECK(set.find(set.next_id() - 1)->colour == kProfileColours[0]);
    CHECK(set.unused_colour() == kProfileColours[1]);
}

TEST_CASE("profile: rename and recolour change one profile and keep the rules") {
    ProfileSet set;
    REQUIRE(set.add("Anna", 0) == ProfileResult::Ok);
    CHECK(set.rename(kDefaultProfileId, "Mum") == ProfileResult::Ok); // the default can be renamed
    CHECK(set.default_profile().name == "Mum");
    CHECK(set.rename(2, "mum") == ProfileResult::NameTaken);
    CHECK(set.rename(2, "Anna") == ProfileResult::Ok); // its own name again is no clash
    CHECK(set.rename(2, " ") == ProfileResult::EmptyName);
    CHECK(set.rename(9, "Zed") == ProfileResult::NoSuchProfile);
    CHECK(set.recolour(2, 0xABCDEF) == ProfileResult::Ok);
    CHECK(set.find(2)->colour == 0xABCDEF);
    CHECK(set.recolour(2, 0x1FFFFFF) == ProfileResult::Ok);
    CHECK(set.find(2)->colour == 0xFFFFFF);
    CHECK(set.recolour(9, 1) == ProfileResult::NoSuchProfile);
}

TEST_CASE("profile: stored rows are repaired, the default first and always present") {
    std::vector<Profile> rows;
    rows.push_back({1, "Mum", 0x4C9BE8, 1});
    rows.push_back({5, "Zoe", 0x112233, 7});
    rows.push_back({5, "Zoe again", 0x112233, 8}); // a repeated id
    rows.push_back({6, "zoe", 0x445566, 9});       // a repeated name
    rows.push_back({0, "Nobody", 0, 0});           // not an id
    rows.push_back({3, "", 0x1000000, 2});         // no name, colour out of range
    const ProfileSet set(rows);
    REQUIRE(set.size() == 3);
    CHECK(set.all()[0].id == kDefaultProfileId);
    CHECK(set.find(5)->name == "Zoe");
    CHECK(set.find(3)->name == "Player");
    CHECK(set.find(3)->colour == 0);
    CHECK(set.next_id() == 6);
}

TEST_CASE("profile: rows without the default profile get it added first") {
    std::vector<Profile> rows;
    rows.push_back({4, "Dani", 0x1, 1});
    const ProfileSet set(rows);
    REQUIRE(set.size() == 2);
    CHECK(set.all()[0].id == kDefaultProfileId);
    CHECK(set.all()[1].id == 4);
}

TEST_CASE("profile: the default profile wins its name over another row") {
    std::vector<Profile> rows;
    rows.push_back({2, "Player", 0x1, 1});
    rows.push_back({1, "Player", 0x2, 1});
    const ProfileSet set(rows);
    REQUIRE(set.size() == 1);
    CHECK(set.default_profile().colour == 0x2);
}

TEST_CASE("profile: a set never grows past the household size when read back") {
    std::vector<Profile> rows;
    for (int i = 1; i <= 40; ++i) {
        rows.push_back({i, "P" + std::to_string(i), 0, 0});
    }
    const ProfileSet set(rows);
    CHECK(set.size() == kMaxProfiles);
    CHECK(set.contains(kDefaultProfileId));
}

TEST_CASE("profile delete: the only profile and the first one stay, the rest can go") {
    ProfileSet set;
    CHECK(set.check_delete(kDefaultProfileId) == ProfileResult::LastProfile);
    CHECK(set.check_delete(9) == ProfileResult::NoSuchProfile);
    Profile anna;
    Profile ben;
    REQUIRE(set.add("Anna", 10, &anna) == ProfileResult::Ok);
    REQUIRE(set.add("Ben", 20, &ben) == ProfileResult::Ok);
    CHECK(set.check_delete(kDefaultProfileId) == ProfileResult::FirstProfile);
    CHECK(set.check_delete(anna.id) == ProfileResult::Ok);
    CHECK(set.remove(kDefaultProfileId) == ProfileResult::FirstProfile);
    CHECK(set.size() == 3); // nothing changed
    CHECK(set.remove(anna.id) == ProfileResult::Ok);
    CHECK(set.size() == 2);
    CHECK_FALSE(set.contains(anna.id));
    CHECK(set.find(ben.id)->name == "Ben"); // the others are as they were
    CHECK(set.remove(anna.id) == ProfileResult::NoSuchProfile);
    CHECK(set.remove(ben.id) == ProfileResult::Ok);
    CHECK(set.remove(kDefaultProfileId) == ProfileResult::LastProfile); // the last one stays
    CHECK(set.size() == 1);
    // An id is not used again by the next profile made after a delete of the newest.
    Profile again;
    REQUIRE(set.add("Cy", 30, &again) == ProfileResult::Ok);
    CHECK(set.contains(again.id));
}

TEST_CASE("profile successor: the most recently used of the others, else the newest made, never the "
          "one leaving") {
    ProfileSet set;
    Profile a;
    Profile b;
    Profile c;
    REQUIRE(set.add("Anna", 100, &a) == ProfileResult::Ok);
    REQUIRE(set.add("Ben", 200, &b) == ProfileResult::Ok);
    REQUIRE(set.add("Cy", 300, &c) == ProfileResult::Ok);
    const std::vector<ProfileUse> none;
    CHECK(set.successor(c.id, none) == b.id); // none used: the latest made of the rest
    const std::vector<ProfileUse> uses{{a.id, 900}, {b.id, 400}, {kDefaultProfileId, 500}};
    CHECK(set.successor(c.id, uses) == a.id);
    CHECK(set.successor(a.id, uses) == kDefaultProfileId);
    const std::vector<ProfileUse> tied{{a.id, 700}, {b.id, 700}};
    CHECK(set.successor(c.id, tied) == a.id); // equal: the earlier one in the household
    ProfileSet alone;
    CHECK(set.successor(a.id, uses) != a.id);
    CHECK(alone.successor(kDefaultProfileId, none) == kDefaultProfileId);
}

TEST_CASE("profile delete: the typed name confirms only the exact name, spaces at the ends aside") {
    CHECK(confirms_profile_name("Dani", "Dani"));
    CHECK(confirms_profile_name("  Dani", "Dani"));    // leading blanks
    CHECK(confirms_profile_name("Dani   ", "Dani"));   // trailing blanks
    CHECK(confirms_profile_name(" Dani ", "Dani"));
    CHECK_FALSE(confirms_profile_name("dani", "Dani")); // case matters
    CHECK_FALSE(confirms_profile_name("DANI", "Dani"));
    CHECK_FALSE(confirms_profile_name("Dan", "Dani"));  // partial
    CHECK_FALSE(confirms_profile_name("Danii", "Dani"));
    CHECK_FALSE(confirms_profile_name("", "Dani"));
    CHECK_FALSE(confirms_profile_name("   ", "Dani"));
    CHECK_FALSE(confirms_profile_name("Da ni", "Dani")); // a blank inside is not trimmed
    CHECK_FALSE(confirms_profile_name("Dani", ""));      // nothing is confirmed by an empty name
    CHECK_FALSE(confirms_profile_name("", ""));
    // Names with spaces inside keep them.
    CHECK(confirms_profile_name("Mary Ann ", "Mary Ann"));
    CHECK_FALSE(confirms_profile_name("Mary  Ann", "Mary Ann"));
}

TEST_CASE("profile delete: multi-byte names match by their bytes, whole or not at all") {
    const std::string asa = "\xC3\x85sa";           // Åsa
    const std::string lower = "\xC3\xA5sa";         // åsa
    const std::string kanji = "\xE6\x97\xA5\xE6\x9C\xAC"; // 日本
    CHECK(confirms_profile_name(asa, asa));
    CHECK(confirms_profile_name(" " + asa + " ", asa));
    CHECK_FALSE(confirms_profile_name(lower, asa));      // a different letter case
    CHECK_FALSE(confirms_profile_name("\xC3sa", asa));   // half of the first character
    CHECK_FALSE(confirms_profile_name("sa", asa));
    CHECK(confirms_profile_name(kanji, kanji));
    CHECK_FALSE(confirms_profile_name(kanji.substr(0, 4), kanji)); // a character and a half
    CHECK_FALSE(confirms_profile_name(kanji.substr(0, 3), kanji)); // one of two characters
    // The no-break space is not a blank to trim: it is part of what was typed.
    CHECK_FALSE(confirms_profile_name("\xC2\xA0" + asa, asa));
}
