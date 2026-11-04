#pragma once
// TOP OF THE MONTH and the VERIFIED list, on their own (no downloads in them), to be tested.
//
// TOP OF THE MONTH goes by what a pack did this month, not by what it has done since it came out. Thunderstore keeps a
// count for each version of a package and the day the version came out, so the downloads of the versions put out this
// month are this month's. The downloads the older versions still get are counted from the first time this PC saw them
// this month (a base kept in market/month.json, started again each month). Likes the same way: from the first count
// seen this month, all of them for a pack that came out this month.
//
// VERIFIED is the developers' word, kept in the studio's GitHub repository (verified.json on its market-data branch) and
// read by every studio: the packs they vouch for (Team-Name), and makers all of whose packs they do (Team). Only those
// who can write to the repository can change it, so nobody can verify themselves.
#include "marketplace.h"
#include "marketplace_listing.h"
#include "Engine/Core/Json/json.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::anim_studio::market {
// ---- this month ----
// A version's downloads, and the day it came out ("2026-11-01", in UTC as Thunderstore gives it). Its number ("1.2.0")
// for a pack's page, "" in what older studios kept.
struct VersionCount {
    std::string date;
    std::uint64_t downloads{};
    std::string version;
};
using VersionCounts = std::map<std::string, std::vector<VersionCount>>; // a pack (Team-Name) -> its versions
// Each of the studio's packs' versions, from Thunderstore's package listing (the one with every version in it).
inline VersionCounts read_version_counts(const std::string& text) {
    VersionCounts out;
    const auto root = Json::parse(text, JsonLimits{96 * 1024 * 1024, 64, 64 * 1024 * 1024});
    if (!root.is_array()) return out;
    for (const auto& package : root) {
        try {
            if (!package.is_object() || !package.contains("versions") || !package.at("versions").is_array() || package.at("versions").size() == 0) continue;
            const auto& newest = package.at("versions").at(0);
            if (newest.value("website_url", "") != studio_url && !newest.value("description", "").starts_with(description_tag)) continue;
            const auto name = package.value("full_name", "");
            if (name.empty() || name.size() > 260) continue;
            auto& versions = out[name];
            for (const auto& v : package.at("versions")) {
                if (!v.is_object()) continue;
                const auto date = v.value("date_created", "");
                if (day_number(date) < 0) continue;
                auto number = v.value("version_number", "");
                if (!plain_version(number)) number.clear();
                versions.push_back({date.substr(0, 10), static_cast<std::uint64_t>(std::max(0.0, v.value("downloads", 0.0))), std::move(number)});
            }
        } catch (...) {}
    }
    return out;
}
// The same, as the studio keeps it between runs ({"Team-Name": [["2026-11-01", 120, "1.0.1"], ...]}; older studios kept
// no number, and theirs is read as well).
inline std::string version_counts_json(const VersionCounts& counts) {
    Json out = Json::object();
    for (const auto& [name, versions] : counts) {
        Json list = Json::array();
        for (const auto& v : versions) list.push_back(Json::array({Json(v.date), Json(v.downloads), Json(v.version)}));
        out[name] = std::move(list);
    }
    return out.dump();
}
inline VersionCounts read_version_counts_json(const std::string& text) {
    VersionCounts out;
    const auto root = Json::parse(text, JsonLimits{16 * 1024 * 1024, 16, 16 * 1024 * 1024});
    if (!root.is_object()) return out;
    for (const auto& [name, list] : root.items()) {
        if (!list.is_array()) continue;
        auto& versions = out[name];
        for (const auto& v : list)
            if (v.is_array() && (v.size() == 2 || v.size() == 3) && v.at(0).is_string() && v.at(1).is_number() && day_number(v.at(0).string()) >= 0) {
                std::string number = v.size() == 3 && v.at(2).is_string() ? v.at(2).string() : std::string{};
                if (!plain_version(number)) number.clear();
                versions.push_back({v.at(0).string().substr(0, 10), static_cast<std::uint64_t>(std::max(0.0, v.at(1).get<double>())), std::move(number)});
            }
    }
    return out;
}

// Where this month's counting starts for each pack: the month ("2026-11"), and the downloads of its older versions and
// its likes when this PC first saw it in that month.
struct MonthBase {
    std::string month;
    std::map<std::string, std::uint64_t> older;
    std::map<std::string, int> likes;
};
inline std::string month_base_json(const MonthBase& base) {
    Json older = Json::object(), likes = Json::object();
    for (const auto& [name, n] : base.older) older[name] = n;
    for (const auto& [name, n] : base.likes) likes[name] = n;
    return Json{{"month", base.month}, {"older", std::move(older)}, {"likes", std::move(likes)}}.dump(2);
}
inline MonthBase read_month_base(const std::string& text) {
    MonthBase base;
    const auto root = Json::parse(text, JsonLimits{4 * 1024 * 1024, 16, 4 * 1024 * 1024});
    if (!root.is_object()) return base;
    base.month = root.value("month", "");
    if (root.contains("older") && root.at("older").is_object())
        for (const auto& [name, n] : root.at("older").items())
            if (n.is_number()) base.older[name] = static_cast<std::uint64_t>(std::max(0.0, n.get<double>()));
    if (root.contains("likes") && root.at("likes").is_object())
        for (const auto& [name, n] : root.at("likes").items())
            if (n.is_number()) base.likes[name] = static_cast<int>(std::max(0.0, n.get<double>()));
    return base;
}
// What a pack did in `month` ("2026-11").
struct MonthNumbers {
    std::uint64_t downloads{};
    int likes{};
    bool counted{}; // its versions were known (or it came out this month): the downloads are this month's, not a guess
};
// The downloads of `versions` put out in `month`, and of those put out before it.
inline std::pair<std::uint64_t, std::uint64_t> split_by_month(const std::vector<VersionCount>& versions, std::string_view month) {
    std::uint64_t fresh = 0, older = 0;
    for (const auto& v : versions) (std::string_view(v.date).substr(0, 7) == month ? fresh : older) += v.downloads;
    return {fresh, older};
}
inline MonthNumbers month_numbers(const Pack& p, const std::vector<VersionCount>* versions, const MonthBase& base, std::string_view month) {
    MonthNumbers out;
    const bool new_this_month = std::string_view(p.created).substr(0, 7) == month;
    if (versions && !versions->empty()) {
        const auto [fresh, older] = split_by_month(*versions, month);
        std::uint64_t grown = 0;
        if (base.month == month)
            if (const auto found = base.older.find(p.full_name); found != base.older.end() && older > found->second) grown = older - found->second;
        out.downloads = fresh + grown;
        out.counted = true;
    } else if (new_this_month) {
        out.downloads = p.downloads; // all of them came this month
        out.counted = true;
    }
    if (new_this_month) out.likes = std::max(0, p.rating);
    else if (base.month == month)
        if (const auto found = base.likes.find(p.full_name); found != base.likes.end()) out.likes = std::max(0, p.rating - found->second);
    return out;
}
// The base brought up to date: a new month starts it again; a pack it does not have yet starts from what it has now.
// True when it changed (to be saved).
inline bool update_month_base(MonthBase& base, const std::vector<Pack>& packs, const VersionCounts& counts, std::string_view month) {
    bool changed = false;
    if (base.month != month) {
        base = MonthBase{std::string(month), {}, {}};
        changed = true;
    }
    for (const auto& p : packs) {
        const bool new_this_month = std::string_view(p.created).substr(0, 7) == month;
        if (!base.likes.contains(p.full_name)) {
            base.likes[p.full_name] = new_this_month ? 0 : std::max(0, p.rating);
            changed = true;
        }
        if (base.older.contains(p.full_name)) continue;
        const auto found = counts.find(p.full_name);
        if (found == counts.end() || found->second.empty()) continue; // started once its versions are known
        base.older[p.full_name] = split_by_month(found->second, month).second;
        changed = true;
    }
    return changed;
}
// How a month's packs are ranked: their downloads this month, a like this month worth 25 of them.
inline std::uint64_t month_score(const MonthNumbers& n) { return n.downloads + 25ull * static_cast<std::uint64_t>(std::max(0, n.likes)); }
// This month, as "2026-11", in UTC (as Thunderstore's dates are) from a count of days since 1970-01-01.
inline std::string month_of_day(long long day) {
    // Howard Hinnant's civil_from_days.
    day += 719468;
    const long long era = (day >= 0 ? day : day - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(day - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const long long year = y + (m <= 2);
    char out[16]{};
    std::snprintf(out, sizeof out, "%04lld-%02u", year, m);
    return out;
}

// ---- VERIFIED ----
inline constexpr char verified_repo[] = "FourthStudio/ReSkate-Animator-Studio";
inline constexpr char verified_branch[] = "market-data";
inline constexpr char verified_file[] = "verified.json";
// Everyone reads it from here (GitHub's raw files: no limit on how often, up to five minutes behind).
inline std::string verified_list_url() {
    return std::string("https://raw.githubusercontent.com/") + verified_repo + "/" + verified_branch + "/" + verified_file;
}
struct VerifiedList {
    std::set<std::string> packs;  // Team-Name
    std::set<std::string> makers; // Team
};
// A pack's full name as Thunderstore writes it: a team's name, a dash, the package's name.
inline bool plain_full_name(std::string_view s) {
    const auto dash = s.find('-');
    return dash != std::string_view::npos && plain_name(s.substr(0, dash)) && plain_name(s.substr(dash + 1));
}
// The list as its file says, every name checked (what is not a name is left out).
inline VerifiedList read_verified(const std::string& text) {
    VerifiedList out;
    const auto root = Json::parse(text, JsonLimits{1024 * 1024, 8, 1024 * 1024});
    if (!root.is_object()) return out;
    const auto take = [&](std::string_view key, std::set<std::string>& into, auto ok) {
        if (!root.contains(key) || !root.at(key).is_array()) return;
        for (const auto& name : root.at(key))
            if (name.is_string() && ok(name.string()) && into.size() < 5000) into.insert(name.string());
    };
    take("packs", out.packs, [](const std::string& s) { return plain_full_name(s); });
    take("makers", out.makers, [](const std::string& s) { return plain_name(s); });
    return out;
}
inline std::string verified_json(const VerifiedList& list) {
    Json packs = Json::array(), makers = Json::array();
    for (const auto& p : list.packs) packs.push_back(p);
    for (const auto& m : list.makers) makers.push_back(m);
    return Json{{"about", "The Animator Studio MARKET's VERIFIED list: the packs (Team-Name) and makers (Team) the studio's developers vouch for. "
                          "Every studio reads it; developers change it from a pack's page in the MARKET, or here."},
                {"packs", std::move(packs)},
                {"makers", std::move(makers)}}
        .dump(2) + "\n";
}
inline bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return (x >= 'A' && x <= 'Z' ? x + 32 : x) == (y >= 'A' && y <= 'Z' ? y + 32 : y);
           });
}
// Whether a pack is VERIFIED: on the list itself, or by a maker on it (or one of verified_makers, built in).
inline bool verified_by(const VerifiedList& list, std::string_view owner, std::string_view full_name) {
    if (verified_maker(owner)) return true;
    for (const auto& m : list.makers)
        if (same_name(m, owner)) return true;
    for (const auto& p : list.packs)
        if (same_name(p, full_name)) return true;
    return false;
}

// ---- base64, both ways (GitHub sends and takes a file's contents in it) ----
inline std::string base64_encode(std::string_view bytes) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) | static_cast<unsigned char>(bytes[i + 2]);
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += table[(n >> 6) & 63], out += table[n & 63];
    }
    if (i + 1 == bytes.size()) {
        const auto n = static_cast<unsigned char>(bytes[i]) << 16;
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += "==";
    } else if (i + 2 == bytes.size()) {
        const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += table[(n >> 6) & 63], out += '=';
    }
    return out;
}
// Anything not of base64 (GitHub breaks it into lines) is passed over.
inline std::string base64_decode(std::string_view text) {
    const auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    unsigned bits = 0;
    int count = 0;
    for (const char c : text) {
        if (c == '=') break;
        const int v = value(c);
        if (v < 0) continue;
        bits = (bits << 6) | static_cast<unsigned>(v);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out += static_cast<char>((bits >> count) & 0xFF);
        }
    }
    return out;
}
} // namespace dingosdk::anim_studio::market
