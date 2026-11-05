// TOP OF THE MONTH's numbers and the developers' VERIFIED list (marketplace_month.h).
#include "Extension/AnimationStudio/marketplace_month.h"
#include <iostream>
#include <stdexcept>

using namespace dingosdk::anim_studio::market;
namespace {
void check(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
Pack pack(const std::string& full_name, const std::string& created, std::uint64_t downloads, int likes) {
    Pack p;
    p.full_name = full_name;
    p.owner = full_name.substr(0, full_name.find('-'));
    p.created = created;
    p.downloads = downloads;
    p.rating = likes;
    return p;
}
// Three packs in Thunderstore's listing, one not the studio's: each version's downloads and the day it came out.
const char* listing = R"([
  {"full_name": "Heatz-Versace", "owner": "Heatz", "name": "Versace", "versions": [
     {"version_number": "1.1.1", "date_created": "2026-11-01T14:20:09Z", "downloads": 1788, "description": "Animator Studio pack: x"},
     {"version_number": "1.1.0", "date_created": "2026-10-30T10:00:00Z", "downloads": 21000, "description": "Animator Studio pack: x"}]},
  {"full_name": "Zest-Zestpack", "owner": "Zest", "name": "Zestpack", "versions": [
     {"version_number": "1.0.0", "date_created": "2026-10-28T10:00:00Z", "downloads": 9000,
      "website_url": "https://github.com/FourthStudio/ReSkate-Animator-Studio", "description": "Zest"}]},
  {"full_name": "Other-Mod", "owner": "Other", "name": "Mod", "versions": [
     {"version_number": "1.0.0", "date_created": "2026-11-01T10:00:00Z", "downloads": 99999, "description": "A mod"}]}
])";

void versions_are_read_for_the_studios_packs_only() {
    const auto counts = read_version_counts(listing);
    check(counts.size() == 2 && counts.contains("Heatz-Versace") && counts.contains("Zest-Zestpack"), "only the studio's packs are read");
    check(counts.at("Heatz-Versace").size() == 2 && counts.at("Heatz-Versace")[0].date == "2026-11-01" && counts.at("Heatz-Versace")[0].downloads == 1788,
          "each version keeps its day and its downloads");
    const auto again = read_version_counts_json(version_counts_json(counts));
    check(again.size() == 2 && again.at("Heatz-Versace")[1].downloads == 21000 && again.at("Zest-Zestpack")[0].date == "2026-10-28",
          "the counts come back as they were kept");
    check(counts.at("Heatz-Versace")[0].version == "1.1.1" && again.at("Heatz-Versace")[1].version == "1.1.0", "each version keeps its number");
    // What an older studio kept (no numbers) still reads.
    const auto old = read_version_counts_json(R"({"Heatz-Versace": [["2026-11-01", 1788], ["2026-10-30", 21000]]})");
    check(old.at("Heatz-Versace").size() == 2 && old.at("Heatz-Versace")[0].version.empty() && old.at("Heatz-Versace")[1].downloads == 21000,
          "counts kept without numbers are read");
}
// The shelves by trick type: each README line's kind, from its name and the game's trick it plays on.
void tricks_are_sorted_by_kind() {
    const std::pair<const char*, TrickKind> lines[]{
        {"Kickflip (mid pop)", TrickKind::flip},
        {"Steezy Flip - Kickflip (mid pop, fakie)", TrickKind::flip},
        {"FF Impossible high switch fakie 3 (high pop)", TrickKind::flip},
        {"mid steez tre (mid pop)", TrickKind::flip},
        {"nollie heel low sketch landing - Nollie Heelflip (low pop)", TrickKind::flip},
        {"Rocket Shuv - Pop Shuv-it (any pop)", TrickKind::shuvit},
        {"Nollie BS Bigspin Shuv (mid pop)", TrickKind::shuvit},
        {"Ollie HIGH fakie (high pop, fakie)", TrickKind::ollie},
        {"Grind BF FS Feeble BS Smith Rail Entry (any pop)", TrickKind::grind},
        {"Cross Lock 5050 (any pop)", TrickKind::grind},
        {"Grind BS Noseslide Rail Entry (any pop)", TrickKind::grind},
        {"Manual N Entry (any pop)", TrickKind::manual},
        {"Land Fakie Revert 45 BS Medium (any pop)", TrickKind::landing},
        {"Powerslide BS Fast Loop (any pop)", TrickKind::landing},
        {"In Air FS Grab Entry (any pop)", TrickKind::grab},
        {"Leg extend (any pop)", TrickKind::other},
        {"Block party (any pop)", TrickKind::other}, // "lock" inside a word is not a lock
    };
    for (const auto& [line, kind] : lines) check(trick_kind(line) == kind, line);
    const auto counts = trick_kinds({"Kickflip (mid pop)", "Heelflip (low pop)", "Manual (any pop)"});
    check(counts[static_cast<std::size_t>(TrickKind::flip)] == 2 && counts[static_cast<std::size_t>(TrickKind::manual)] == 1 &&
              counts[static_cast<std::size_t>(TrickKind::grind)] == 0,
          "a pack's tricks are counted by kind");
}
// The all-time biggest is not the month's biggest: only what was downloaded this month counts.
void the_month_counts_this_months_downloads_not_all_time() {
    const auto counts = read_version_counts(listing);
    const std::vector<Pack> packs{pack("Heatz-Versace", "2026-10-25", 22788, 6), pack("Zest-Zestpack", "2026-10-28", 9000, 3)};
    MonthBase base;
    check(update_month_base(base, packs, counts, "2026-11"), "a new month starts the base");
    check(base.month == "2026-11" && base.older.at("Heatz-Versace") == 21000 && base.older.at("Zest-Zestpack") == 9000, "older versions start where they are");
    auto versace = month_numbers(packs[0], &counts.at("Heatz-Versace"), base, "2026-11");
    auto zest = month_numbers(packs[1], &counts.at("Zest-Zestpack"), base, "2026-11");
    check(versace.counted && versace.downloads == 1788 && versace.likes == 0, "this month's version counts in full, nothing else yet");
    check(zest.counted && zest.downloads == 0, "a pack with nothing new this month starts at nothing");
    // Later in the month: Zest's old version is downloaded 2500 more times and liked 4 more; Versace's new one 100 more.
    VersionCounts later = counts;
    later.at("Zest-Zestpack")[0].downloads = 11500;
    later.at("Heatz-Versace")[0].downloads = 1888;
    const auto zest_later = pack("Zest-Zestpack", "2026-10-28", 11500, 7);
    check(!update_month_base(base, {packs[0], zest_later}, later, "2026-11"), "the base does not move within the month");
    zest = month_numbers(zest_later, &later.at("Zest-Zestpack"), base, "2026-11");
    versace = month_numbers(packs[0], &later.at("Heatz-Versace"), base, "2026-11");
    check(zest.downloads == 2500 && zest.likes == 4, "older versions count what they got since the month began");
    check(month_score(zest) == 2500 + 100 && month_score(versace) == 1888, "a like is worth 25 downloads");
    check(month_score(versace) < month_score(zest), "the month's top is the month's, though Versace has more downloads in all");
}
void a_pack_new_this_month_counts_everything() {
    const auto fresh = pack("New-Pack", "2026-11-03", 640, 5);
    MonthBase base;
    update_month_base(base, {fresh}, {}, "2026-11");
    const auto n = month_numbers(fresh, nullptr, base, "2026-11");
    check(n.counted && n.downloads == 640 && n.likes == 5, "out this month: all its downloads and likes are this month's");
    const auto old = pack("Old-Pack", "2026-10-03", 9000, 5);
    const auto unknown = month_numbers(old, nullptr, base, "2026-11");
    check(!unknown.counted && unknown.downloads == 0, "an older pack whose versions are not known yet is not guessed at");
}
void a_new_month_starts_again() {
    const auto counts = read_version_counts(listing);
    MonthBase base;
    update_month_base(base, {pack("Heatz-Versace", "2026-10-25", 22788, 6)}, counts, "2026-11");
    check(update_month_base(base, {pack("Heatz-Versace", "2026-10-25", 22788, 6)}, counts, "2026-12") && base.month == "2026-12", "December starts again");
    check(base.older.at("Heatz-Versace") == 22788, "every version is older in December");
    const auto again = read_month_base(month_base_json(base));
    check(again.month == "2026-12" && again.older.at("Heatz-Versace") == 22788 && again.likes.at("Heatz-Versace") == 6, "the base comes back as kept");
}
void months_are_named_from_days() {
    check(month_of_day(day_number("2026-11-01")) == "2026-11", "November");
    check(month_of_day(day_number("2026-12-31")) == "2026-12", "December");
    check(month_of_day(day_number("2027-01-01")) == "2027-01", "January");
    check(month_of_day(day_number("2028-02-29")) == "2028-02", "a leap day");
}
void the_verified_list_holds_only_names() {
    const auto list = read_verified(R"({"packs": ["Heatz-VersacePlug_Pack", "not a name", "Bad-", "x/../y-z", 4],
                                       "makers": ["Worthless", "two words", "Fourth_Studio"]})");
    check(list.packs.size() == 1 && list.packs.contains("Heatz-VersacePlug_Pack"), "packs are Team-Name, nothing else");
    check(list.makers.size() == 2 && list.makers.contains("Worthless") && list.makers.contains("Fourth_Studio"), "makers are team names, nothing else");
    check(verified_by(list, "heatz", "heatz-versaceplug_pack"), "a pack on the list is verified, in any case");
    check(verified_by(list, "worthless", "Worthless-steez"), "a maker on the list verifies all their packs");
    check(!verified_by(list, "Heatz", "Heatz-Other_Pack"), "another pack by the same maker is not");
    const auto again = read_verified(verified_json(list));
    check(again.packs == list.packs && again.makers == list.makers, "the list is written back as it was read");
    check(read_verified("[]").packs.empty() && verified_list_url().starts_with("https://raw.githubusercontent.com/FourthStudio/"),
          "an odd file is an empty list; it is read from the studio's repository");
}
void base64_goes_both_ways() {
    for (const std::string text : {"", "a", "ab", "abc", "abcd", "{\"packs\": [\"Heatz-Versace\"]}\n"})
        check(base64_decode(base64_encode(text)) == text, "base64 there and back");
    check(base64_decode("eyJwYWNr\ncyI6W119\n") == "{\"packs\":[]}", "GitHub's lines are passed over");
}
} // namespace

int main() {
    try {
        versions_are_read_for_the_studios_packs_only();
        tricks_are_sorted_by_kind();
        the_month_counts_this_months_downloads_not_all_time();
        a_pack_new_this_month_counts_everything();
        a_new_month_starts_again();
        months_are_named_from_days();
        the_verified_list_holds_only_names();
        base64_goes_both_ways();
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "marketplace month tests passed\n";
    return 0;
}
