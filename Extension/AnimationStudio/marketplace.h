#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The Animator Studio's MARKET: animation packs made in the studio and shared on Thunderstore (thunderstore.io/c/reskate),
// browsed, previewed and installed without leaving the game, and the player's own packs published there from the studio.
//
// Only the studio's own packs are listed: a package is one when its manifest points at the studio (studio_url) or its
// description starts with the studio's tag - which the studio's own export writes, and nothing else does. Nothing is
// downloaded from anywhere but thunderstore.io and its hosts, and a downloaded pack goes through the studio's import,
// which takes nothing but the studio's own animations, packs and pictures.
//
// Publishing needs a Thunderstore team's service account token (Thunderstore has no sign-in for other apps): made on
// thunderstore.io under the team's settings, pasted in once, kept encrypted for this Windows user (DPAPI), and sent to
// thunderstore.io only, with the upload itself.
namespace dingosdk::anim_studio::market {
inline constexpr char community[] = "reskate";
inline constexpr char site[] = "https://thunderstore.io";
// The community's section for the studio's packs (Thunderstore's "Animation Packs" category): every pack published from
// the studio goes in it, and the MARKET lists what is in it besides what Thunderstore's search finds.
inline constexpr char category[] = "animation-packs";
// What marks a package as the studio's: its manifest's website_url, and the start of its description.
inline constexpr char studio_url[] = "https://github.com/FourthStudio/ReSkate-Animator-Studio";
inline constexpr char description_tag[] = "Animator Studio pack: ";

// THE CREW: the testers who shaped the Animator Studio, by their Thunderstore team. Their packs are marked CREW
// CERTIFIED, come first in the shop's own picks, and each of them has a page of their own in the MARKET. Kept here, in
// the build and not in a file, so nobody can make themselves crew: a pack's team is the one Thunderstore published it
// under, which only that team's members can publish to. A new tester comes in with a new build.
struct CrewMember {
    std::string_view team, name, about;
    // Their pictures are built in when they sent them (assets/animator_studio/crew/<team>_profile.jpg and _thumbnail.jpg);
    // a thumbnail is cut to each card's shape round this point of it (0..1 across, 0..1 down).
    float focus_x{0.5f}, focus_y{0.5f};
};
inline constexpr std::array crew_members{
    CrewMember{"leafbro", "leaf.bro", "Bailed into every bug so you don't have to. Hall of Meat regular.", 0.48f, 0.42f},
    CrewMember{"Myawsometeam", "Setsu", "Ran the same line a hundred times so the studio lands it a hundred and one.", 0.56f, 0.32f},
    CrewMember{"FIENDSKATEBOARDS", "renzoh.", "Pop shuv-it specialist: sent the dump and the video before the board even landed.", 0.5f, 0.42f},
    CrewMember{"smonky", "smonk", "Rolls away from every crash, then sends the log. Smooth as fresh bearings.", 0.5f, 0.38f},
    CrewMember{"reskate_animation_editor", "philsteezy", "Steezy bug reports only. Even the crashes had style.", 0.5f, 0.4f},
    CrewMember{"Heatz", "Heatz", "Brought the heat every session: if a build could break, Heatz broke it first.", 0.5f, 0.36f},
    CrewMember{"Bonksz", "Bonksz", "Bonked every rail, ledge and kicker until the bugs gave up.", 0.6f, 0.42f},
    CrewMember{"kanga", "Kanga", "Hops every gap first: if a trick bounces off a bug, the log is already in your DMs.", 0.66f, 0.42f},
    CrewMember{"Style_Over_Score", "Style over Score", "Steez before points: filmed every trick until it looked right, and every bug that didn't.", 0.5f, 0.5f},
};
// BUILT BY, on ABOUT: who made the Animator Studio. Their pictures are found by `pictures` (assets/animator_studio/crew/
// <pictures>_profile.jpg and _thumbnail.jpg, as THE CREW's); `team` is their Thunderstore team where there is one to link to;
// `about`, a line on their card.
struct Builder {
    std::string_view name, pictures, team, about;
    float focus_x{0.5f}, focus_y{0.5f}; // where their shot is cut round, as CrewMember's
};
inline constexpr std::array builders{
    Builder{"22SH", "22sh", "", "Animation pipeline wizard. Built the internal tools that extract and bring every trick into the game."},
    Builder{"Fourth", "fourth", "Fourth_Studio", "Lead studio developer. Brought the team together and built the studio around it.", 0.5f, 0.45f},
    // Norms' Thunderstore team: none yet; put "worthlessnorms" here once it exists.
    Builder{"Norms", "norms", "", "Scripting and animation logic: the rules that decide what plays, and when.", 0.5f, 0.39f},
    Builder{"Steez", "steez", "Worthless", "Made the animations run smooth, from the pop to the landing.", 0.48f, 0.4f},
};
// One of BUILT BY by their Thunderstore team (in any case), or none: their packs are stamped DEV.
inline const Builder* builder_of(std::string_view team) {
    if (team.empty()) return nullptr;
    for (const auto& builder : builders)
        if (builder.team.size() == team.size() && std::equal(team.begin(), team.end(), builder.team.begin(), [](char a, char b) {
                return (a >= 'A' && a <= 'Z' ? a + 32 : a) == (b >= 'A' && b <= 'Z' ? b + 32 : b);
            }))
            return &builder;
    return nullptr;
}
// VERIFIED makers built in, by their Thunderstore team: their packs carry the yellow VERIFIED tag and have a shelf of their
// own on the MARKET's front page. The developers' list on GitHub (marketplace_month.h, verified_list_url) says the rest.
inline constexpr std::array<std::string_view, 0> verified_makers{};
inline bool verified_maker(std::string_view team) {
    if (team.empty()) return false;
    for (const auto& maker : verified_makers)
        if (maker.size() == team.size() && std::equal(team.begin(), team.end(), maker.begin(), [](char a, char b) {
                return (a >= 'A' && a <= 'Z' ? a + 32 : a) == (b >= 'A' && b <= 'Z' ? b + 32 : b);
            }))
            return true;
    return false;
}
inline const CrewMember* crew_member(std::string_view team) {
    for (const auto& member : crew_members)
        if (member.team.size() == team.size() && std::equal(team.begin(), team.end(), member.team.begin(), [](char a, char b) {
                return (a >= 'A' && a <= 'Z' ? a + 32 : a) == (b >= 'A' && b <= 'Z' ? b + 32 : b);
            }))
            return &member;
    return nullptr;
}

// ---- what kind of tricks a pack holds (the MARKET's shelves by trick type) ----
// A trick's kind, from its line in the pack's README (its name, the game's trick it plays on, then its pop and stance in
// brackets: "Steezy Flip - Kickflip (mid pop, fakie)"). Read from the words, since a pack's own names are free. The order
// they are tried in matters: grinds and slides first (a lock, a rail), manuals, then flips (an impossible, a tre and a
// "heel" are flips, a "sketchy landing" flip still a flip), shuv-its, then landings, reverts and powerslides, grabs,
// ollies and nollies, and the rest. The kinds are listed in the order the shop shows them.
enum class TrickKind : std::uint8_t { flip, shuvit, ollie, grind, manual, grab, landing, other };
inline constexpr std::size_t trick_kind_count = 8;
inline constexpr std::array<const char*, trick_kind_count> trick_kind_names{"FLIPS", "SHUV-ITS", "OLLIES", "GRINDS & SLIDES", "MANUALS", "GRABS",
                                                                           "LANDINGS & REVERTS", "OTHER"};
using KindCounts = std::array<std::uint16_t, trick_kind_count>;
inline TrickKind trick_kind(std::string_view line) {
    // The name only: the last bracket is its pop and stance.
    if (const auto open = line.rfind(" ("); open != std::string_view::npos && line.ends_with(")")) line = line.substr(0, open);
    std::string low;
    low.reserve(line.size() + 2);
    low += ' ';
    for (const char c : line) {
        const auto u = static_cast<unsigned char>(c);
        low += (u < 128 && std::isalnum(u)) ? static_cast<char>(std::tolower(u)) : ' ';
    }
    low += ' ';
    const auto has = [&](std::string_view part) { return low.find(part) != std::string::npos; };
    // A whole word (or the word and an s): "lock" in "cross lock", not in "block".
    const auto word = [&](std::string_view w) {
        for (auto at = low.find(w); at != std::string::npos; at = low.find(w, at + 1)) {
            const char after = low[at + w.size()]; // there is always the closing space
            if (low[at - 1] == ' ' && (after == ' ' || (after == 's' && low[at + w.size() + 1] == ' '))) return true;
        }
        return false;
    };
    const bool powerslide = has("powerslide") || has("power slide");
    if (has("grind") || (has("slide") && !powerslide) || word("5050") || has(" 50 50 ") || has(" 5 0 ") || word("smith") || word("feeble") ||
        word("crook") || word("crooked") || word("overcrook") || word("salad") || word("suski") || word("willy") || word("rail") || word("lock") ||
        word("ledge") || word("blunt") || word("noseblunt") || word("crosslock") || word("smif"))
        return TrickKind::grind;
    if (word("manual") || word("manny") || word("nosemanual")) return TrickKind::manual;
    if (has("flip") || word("impossible") || word("imp") || word("tre") || word("lazer") || word("laser") || word("casper") || word("varial") ||
        word("heel") || word("vheel") || word("kick") || word("vkick") || word("inward") || word("inw") || word("hard") || word("kf") ||
        word("hf"))
        return TrickKind::flip;
    if (has("shuv") || has("shove") || has("bigspin") || has("big spin")) return TrickKind::shuvit;
    if (word("revert") || word("land") || word("landing") || powerslide) return TrickKind::landing;
    if (word("grab") || word("indy") || word("melon") || word("stalefish") || word("method") || word("mute") || word("airwalk") ||
        word("benihana") || word("judo") || word("madonna") || word("roastbeef") || word("crail") || word("japan") || word("christ"))
        return TrickKind::grab;
    if (word("ollie") || word("nollie")) return TrickKind::ollie;
    return TrickKind::other;
}
// How many of a pack's tricks are of each kind.
inline KindCounts trick_kinds(const std::vector<std::string>& tricks) {
    KindCounts out{};
    for (const auto& t : tricks) {
        auto& n = out[static_cast<std::size_t>(trick_kind(t))];
        if (n < 65535) ++n;
    }
    return out;
}

// One of a pack's versions on Thunderstore: its number ("" when not known), the day it came out, its downloads.
struct PackVersion {
    std::string version, date;
    std::uint64_t downloads{};
};

struct Pack {
    std::string full_name;      // Owner-Name
    std::string owner, name;    // the team, and the package's name
    std::string title;          // the name, with spaces
    std::string version;        // its newest
    std::string description;    // its short description, without the studio's tag
    std::string icon_url, download_url, package_url;
    std::string updated;        // when its newest version came out ("2026-10-23")
    std::string created;        // when it first came out ("2026-10-23"): TOP OF THE WEEK and OF THE MONTH go by it
    std::uint64_t downloads{};  // every version's together
    int rating{};
    std::string icon_file;      // its icon once downloaded (UTF-8 path), "" until then
    bool favorite{}, mine{};    // favorite: liked here (the heart)
    bool crew{};                // made by one of THE CREW, or one of BUILT BY (with `dev`)
    bool dev{};                 // made by one of BUILT BY: stamped DEV, not CREW CERTIFIED
    bool verified{};            // VERIFIED by the studio's developers (the pack, or its maker), or by a maker built in
    bool section{};             // listed in the community's Animation Packs section
    // What it did this month (marketplace_month.h): its downloads and likes since the month began. `month_counted` false:
    // its versions are not known yet, so the downloads are not this month's (TOP OF THE MONTH leaves it out until they are).
    std::uint64_t month_downloads{};
    int month_likes{};
    bool month_counted{};
    std::string installed;      // the version installed here, "" none
    std::string installed_as;   // the pack it became in the library ("" not known: installed before this was kept)
    // Its README, read in the background for every pack (so a search finds a pack by the tricks in it): what its maker
    // says of it and its tricks, a line each. `readme` 0 not read yet, 1 being read, 2 read, 3 could not be read.
    std::string about;
    std::vector<std::string> tricks;
    int readme{};
    std::string stance; // what its tag says (ALL STANCES, GOOFY...), "" not known
    KindCounts kinds{}; // its tricks by kind (trick_kind), once its README is read
    // Its versions, newest first (from the full listing TOP OF THE MONTH reads): empty until that has been read once.
    std::vector<PackVersion> versions;
};
struct Detail {
    std::string full_name, version;
    bool loading{}, ok{};
    std::string about;               // its description in full (README)
    std::vector<std::string> tricks; // what is in it, a line each (README)
    std::string error;
};
// A pack's tricks to watch before it is got (want_preview): its zip downloaded in the background and its tricks taken
// out of it into a folder of their own, never into the library.
struct PreviewTrick {
    std::string name, label;     // its name in the pack, and the name it is shown by
    std::string trick, pop;      // the game's trick it plays for, and its pop ("" any)
    bool switch_stance{}, fakie{};
    std::filesystem::path json, clip;
};
struct Preview {
    std::string full_name, version; // the pack (Team-Name) and its version, "" none asked for
    int state{};                    // 0 none, 1 being downloaded, 2 ready, 3 could not be had (`error` says why)
    float fraction{};               // how far the download is
    std::string error;
    std::vector<PreviewTrick> tricks;
};
// A download or an upload in the background, and how far it has got.
struct Job {
    bool running{}, failed{};
    std::string what;    // "Installing Team-Pack", "Publishing ..."
    float fraction{};
    std::string message; // how the last one went
    std::string url;     // a published pack's page
};
struct View {
    bool loading{};
    std::string error;
    std::vector<Pack> packs;
    std::string team; // the linked Thunderstore team, "" none
    // That team on Thunderstore (check_team): 0 not looked at yet, 1 being looked up, 2 found, 3 no team of that name (a
    // pack's name typed for it), 4 Thunderstore could not be reached.
    int team_state{};
    // Its token, checked with Thunderstore to publish under that team (verify_account): 0 not checked yet, 1 being checked,
    // 2 it does (the team is yours), 3 it does not (another team's token, or not a token), 4 Thunderstore not reached.
    int account_state{};
    bool like_prompt{true}; // a like asks to like it on Thunderstore too
    // The developer key (a GitHub token that can write to the studio's repository), which lets VERIFY be pressed on a
    // pack's page: 0 none on this PC, 1 being checked, 2 it can (`dev_login` is whose), 3 GitHub said no (not a key, or
    // one that cannot write there), 4 GitHub could not be reached.
    int dev_state{};
    std::string dev_login, dev_error;
    std::string month;      // the month TOP OF THE MONTH is of ("2026-11", UTC)
    bool month_known{};     // the packs' versions have been read (this month's numbers are counted, not guessed)
    std::size_t verified_count{}; // packs and makers on the developers' VERIFIED list
    int look{};             // the front page's look: 0 a new one each week, else the one chosen (1 on)
    Job job;
    Detail detail;
    Preview preview;
};
View view();
// The listing, fetched in the background: on opening the MARKET (when it is older than a few minutes) or when asked.
void refresh(bool force);
// Each pack's icon downloaded in the background, for the cards.
void want_icons(const std::vector<std::string>& full_names);
// The packs' READMEs read in the background (kept on disk by version): a pack's `about` and `tricks` once read.
void want_readmes(const std::vector<std::string>& full_names);
void set_favorite(const std::string& full_name, bool on); // the heart: liked here
// Whether a like asks to like the pack on Thunderstore too. (Thunderstore takes likes only from a person signed in on
// its site, never from an app's token, so the studio opens the pack's page there for it.)
void set_like_prompt(bool on);
// The pack's page on Thunderstore, in the browser: thunderstore.io only.
void open_page(const std::string& full_name);
// A team's page on Thunderstore (its packs in the ReSkate community): THE CREW's link on ABOUT.
inline std::string team_page(std::string_view team) { return std::string(site) + "/c/" + community + "/p/" + std::string(team) + "/"; }
void open_team_page(std::string_view team); // in the browser; a team's name of letters, figures and _ only
// Downloads the pack and puts it in the library (the studio's import), in the background.
void install(const std::string& full_name);
// The packs installed by install() since this was last asked, by the names they have in the library, taken (each is
// given once): the LIBRARY asks whether to add each to a playlist. Any thread.
std::vector<std::string> take_fresh_installs();
// Downloads the pack to watch its tricks first (View::preview), in the background: one pack at a time, the last asked
// for. The last few downloaded are kept, so watching again is at once, and GET after it downloads nothing again.
void want_preview(const std::string& full_name);
// A pack of the library is gone (deleted, or its last trick deleted): the package it came from no longer counts as
// installed, so the MARKET offers GET again. Packs the ReSkate launcher installs (Mods) stay: its mod list holds those.
void forget_installed(const std::string& library_pack);
// A pack of the library was renamed: an UPDATE still replaces it, and deleting it still counts.
void installed_pack_renamed(const std::string& from, const std::string& to);
// Links a Thunderstore team for publishing: its name and a service account token of it.
bool link_account(const std::string& team, const std::string& token, std::string& message);
void unlink_account();
// Looks the linked team up on Thunderstore in the background (View::team_state); `again` looks again though it was.
void check_team(bool again = false);
// Asks Thunderstore, in the background, whether the linked token can publish under the linked team (View::account_state):
// its upload check of a package's manifest, with nothing uploaded. Only then is a pack of that team's yours on any PC.
void verify_account(bool again = false);
// The linked team once its token is checked to be that team's, else "" (and the check is started when it has not been).
std::string verified_team();
// Goes up whenever verified_team() changes: who owns which pack is worked out again.
unsigned account_generation();
// ---- VERIFIED, for the studio's developers ----
// Keeps the developer key (encrypted for this Windows user, as the Thunderstore token) and asks GitHub, in the background,
// whether it can write to the studio's repository (View::dev_state). False, with why, when it is not a token at all.
bool link_dev_key(const std::string& token, std::string& message);
void unlink_dev_key();
void check_dev_key(bool again = false);
// A pack put on the developers' VERIFIED list, or taken off it, in the background (as a job): written to verified.json
// on the repository's market-data branch (the branch is made the first time), and everyone's MARKET has it within minutes.
void set_verified(const std::string& full_name, bool on);
// The front page's look (View::look): 0 a new one each week, else the one chosen. Kept with the MARKET's settings.
void set_look(int look);
// The Thunderstore team a pack in the library came from (got in the MARKET or from the launcher's Mods), "" none.
std::string installed_from(const std::string& pack_name);
// Builds the pack as a Thunderstore package and publishes it, in the background. `pack` is a pack of the library;
// `version` the version it goes up as ("" the next one). A pack published before goes to the same package (an update);
// `package` (Team-Name) names the package to update when the pack does not know it (one uploaded by hand).
void publish(const std::string& pack, const std::string& version = {}, const std::string& package = {});
// The same package written to the exports folder, to upload on thunderstore.io by hand. Returns its path, "" when not.
std::string export_package(const std::string& pack, std::string& message, const std::string& version = {});
// Packs installed with the ReSkate launcher (Mods/<Team-Pack> holding reskate-animations.json): put in the library once
// for each version. Any thread.
void take_installed_mods();
// ---- the package's words (on their own, to be tested) ----
// The package's name on Thunderstore for a pack's name (letters, digits and _ only). The README the studio writes, and
// what it reads back out of one.
inline std::string package_name(const std::string& pack) {
    std::string out;
    for (const char c : pack) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (ok) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "Animation_Pack";
    return out.substr(0, 100);
}
inline std::string readme(const std::string& title, const std::string& description, const std::vector<std::string>& tricks, const std::string& made_by) {
    std::string out = "# " + title + "\n\n";
    out += (description.empty() ? std::string("An animation pack made in the ReSkate Animator Studio.") : description) + "\n\n";
    out += "## Tricks\n\n";
    for (const auto& t : tricks) out += "- " + t + "\n";
    out += "\n## Install\n\nOpen the Animator Studio in game (F8), go to MARKET and install it there, or install it with the ReSkate launcher. "
           "It goes straight into the playlist that plays (LIBRARY changes what plays). Each trick above plays on the game's trick "
           "and pop it says, and fills in on the pops the pack has nothing for.\n";
    if (!made_by.empty()) out += "\nMade by " + made_by + " in the ReSkate Animator Studio.\n";
    return out;
}
inline void read_readme(const std::string& markdown, std::string& about, std::vector<std::string>& tricks) {
    about.clear();
    tricks.clear();
    enum { head, text, list, rest } part = head;
    std::size_t start = 0;
    while (start <= markdown.size()) {
        auto end = markdown.find('\n', start);
        if (end == std::string::npos) end = markdown.size();
        std::string line = markdown.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = end + 1;
        if (line.starts_with("# ")) { part = text; continue; }
        if (line.starts_with("## ")) { part = line == "## Tricks" ? list : rest; continue; }
        if (part == text) about += line + "\n";
        else if (part == list && line.starts_with("- ") && tricks.size() < 500) tricks.push_back(line.substr(2));
        if (end == markdown.size()) break;
    }
    while (!about.empty() && (about.back() == '\n' || about.back() == ' ')) about.pop_back();
    while (!about.empty() && about.front() == '\n') about.erase(about.begin());
}

// Thunderstore's names and versions as they may go into an address: a team's or a package's name is letters, digits
// and _, a version digits and dots.
inline bool plain_name(std::string_view s) {
    if (s.empty() || s.size() > 128) return false;
    for (const char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
inline bool plain_version(std::string_view s) {
    if (s.empty() || s.size() > 32 || s.front() == '.' || s.back() == '.') return false;
    for (const char c : s)
        if (!((c >= '0' && c <= '9') || c == '.')) return false;
    return true;
}
// The version a package's icon is of: Thunderstore names it <Team>-<Name>-<version>.png. "" when it is not that.
inline std::string version_from_icon(std::string_view icon_url, std::string_view full_name) {
    const auto slash = icon_url.find_last_of('/');
    if (slash == std::string_view::npos) return {};
    const auto file = icon_url.substr(slash + 1);
    const std::string head = std::string(full_name) + "-";
    if (full_name.empty() || !file.starts_with(head) || !file.ends_with(".png")) return {};
    const auto version = file.substr(head.size(), file.size() - head.size() - 4);
    return plain_version(version) ? std::string(version) : std::string{};
}

// Whether version `a` comes after `b` (1.0.10 after 1.0.9). Versions not in that form come after nothing.
inline bool version_after(const std::string& a, const std::string& b) {
    int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
    if (!plain_version(a) || sscanf_s(a.c_str(), "%d.%d.%d", &a1, &a2, &a3) != 3) return false;
    if (b.empty()) return true;
    if (sscanf_s(b.c_str(), "%d.%d.%d", &b1, &b2, &b3) != 3) return true;
    return a1 != b1 ? a1 > b1 : a2 != b2 ? a2 > b2 : a3 > b3;
}
// Days from 1970-01-01 to a date written 2026-10-23 (only its first ten letters are read); -1 when it is not one.
inline long long day_number(std::string_view date) {
    int y = 0, m = 0, d = 0;
    if (date.size() < 10 || date[4] != '-' || date[7] != '-') return -1;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
        if (date[i] < '0' || date[i] > '9') return -1;
    y = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + (date[3] - '0');
    m = (date[5] - '0') * 10 + (date[6] - '0');
    d = (date[8] - '0') * 10 + (date[9] - '0');
    if (m < 1 || m > 12 || d < 1 || d > 31) return -1;
    // Howard Hinnant's days_from_civil.
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (static_cast<unsigned>(m) + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// The next version: one past the one published last, else the pack's own.
inline std::string next_version(const std::string& version, const std::string& published) {
    int major = 1, minor = 0, patch = 0;
    if (sscanf_s(version.c_str(), "%d.%d.%d", &major, &minor, &patch) != 3) major = 1, minor = 0, patch = 0;
    if (!published.empty() && published == std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch)) ++patch;
    return std::to_string(std::max(0, major)) + "." + std::to_string(std::max(0, minor)) + "." + std::to_string(std::max(0, patch));
}
} // namespace dingosdk::anim_studio::market
