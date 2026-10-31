#pragma once
// The MARKET's listing as Thunderstore gives it, read into packs: on its own (no downloads in it), to be tested.
#include "marketplace.h"
#include "Engine/Core/Json/json.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::anim_studio::market {
// Only thunderstore.io and its hosts are downloaded from (as the launcher does).
inline bool thunderstore_url(std::string_view url) {
    if (!url.starts_with("https://")) return false;
    auto host = url.substr(8);
    host = host.substr(0, host.find_first_of("/?#"));
    return host == "thunderstore.io" || host.ends_with(".thunderstore.io");
}
// One page of Thunderstore's search (what its website lists from, up to date within a minute): the packs on it whose
// description carries the studio's tag, and the next page's address ("" none). A pack whose icon does not say its
// version is left without one, to be asked for (fill_version).
inline std::vector<Pack> read_search(const std::string& text, std::string& next) {
    std::vector<Pack> out;
    next.clear();
    const auto root = Json::parse(text, JsonLimits{8 * 1024 * 1024, 64, 8 * 1024 * 1024});
    if (!root.is_object() || !root.contains("results") || !root.at("results").is_array()) throw std::runtime_error("it is not a listing");
    if (root.contains("next") && root.at("next").is_string()) next = root.at("next").string();
    for (const auto& r : root.at("results")) {
        try {
            if (!r.is_object()) continue;
            // A field missing or empty (null) reads as nothing, not as a pack that cannot be read.
            const auto str = [&](std::string_view key) { return r.contains(key) && r.at(key).is_string() ? r.at(key).string() : std::string{}; };
            const auto num = [&](std::string_view key) { return r.contains(key) && r.at(key).is_number() ? r.at(key).get<double>() : 0.0; };
            const auto yes = [&](std::string_view key) { return r.contains(key) && r.at(key).is_boolean() && r.at(key).get<bool>(); };
            if (yes("is_deprecated") || yes("is_nsfw")) continue;
            const auto description = str("description");
            if (!description.starts_with(description_tag)) continue; // found by the words, but not the studio's
            Pack p;
            p.owner = str("namespace");
            p.name = str("name");
            if (!plain_name(p.owner) || !plain_name(p.name)) continue;
            p.full_name = p.owner + "-" + p.name;
            p.title = p.name;
            std::replace(p.title.begin(), p.title.end(), '_', ' ');
            p.description = description.substr(std::string_view(description_tag).size());
            p.icon_url = str("icon_url");
            if (!thunderstore_url(p.icon_url)) p.icon_url.clear();
            p.version = version_from_icon(p.icon_url, p.full_name);
            if (!p.version.empty()) p.download_url = std::string(site) + "/package/download/" + p.owner + "/" + p.name + "/" + p.version + "/";
            p.package_url = std::string(site) + "/c/" + community + "/p/" + p.owner + "/" + p.name + "/";
            p.updated = str("last_updated").substr(0, 10);
            p.created = str("datetime_created").substr(0, 10);
            if (p.created.empty()) p.created = p.updated;
            p.downloads = static_cast<std::uint64_t>(std::max(0.0, num("download_count")));
            p.rating = static_cast<int>(std::max(0.0, num("rating_count")));
            if (r.contains("categories") && r.at("categories").is_array())
                for (const auto& c : r.at("categories"))
                    if (c.is_object() && c.contains("slug") && c.at("slug").is_string() && c.at("slug").string() == category) p.section = true;
            out.push_back(std::move(p));
        } catch (...) {}
    }
    return out;
}
// The package listing, used when the search fails. Its names become file names (a pack's icon and download), so they are
// held to what read_search asks of them.
inline std::vector<Pack> read_listing(const std::string& text) {
    std::vector<Pack> out;
    const auto root = Json::parse(text, JsonLimits{96 * 1024 * 1024, 64, 64 * 1024 * 1024});
    if (!root.is_array()) return out;
    for (const auto& package : root) {
        try {
            if (!package.is_object() || package.value("is_deprecated", false) || package.value("has_nsfw_content", false)) continue;
            if (!package.contains("versions") || !package.at("versions").is_array() || package.at("versions").size() == 0) continue;
            const auto& newest = package.at("versions").at(0);
            const auto description = newest.value("description", "");
            if (newest.value("website_url", "") != studio_url && !description.starts_with(description_tag)) continue; // not the studio's
            Pack p;
            p.full_name = package.value("full_name", "");
            p.owner = package.value("owner", "");
            p.name = package.value("name", "");
            p.version = newest.value("version_number", "");
            if (!plain_name(p.owner) || !plain_name(p.name) || p.full_name != p.owner + "-" + p.name || !plain_version(p.version)) continue;
            p.title = p.name;
            std::replace(p.title.begin(), p.title.end(), '_', ' ');
            p.description = description.starts_with(description_tag) ? description.substr(std::string_view(description_tag).size()) : description;
            p.icon_url = newest.value("icon", "");
            p.download_url = newest.value("download_url", "");
            p.package_url = package.value("package_url", "");
            p.updated = newest.value("date_created", "").substr(0, 10);
            p.created = package.value("date_created", "").substr(0, 10);
            if (p.created.empty()) p.created = p.updated;
            p.rating = static_cast<int>(package.value("rating_score", 0.0));
            for (const auto& v : package.at("versions")) p.downloads += static_cast<std::uint64_t>(std::max(0.0, v.value("downloads", 0.0)));
            if (package.contains("categories") && package.at("categories").is_array())
                for (const auto& c : package.at("categories"))
                    if (c.is_string() && c.string() == "Animation Packs") p.section = true;
            if (!thunderstore_url(p.icon_url)) p.icon_url.clear();
            if (!thunderstore_url(p.download_url)) continue;
            out.push_back(std::move(p));
        } catch (...) {}
    }
    return out;
}
// A pack of the library has a new name: the MARKET's record of what each package became follows it. True if one did.
inline bool rename_installed_pack(std::map<std::string, std::string>& installed_as, const std::string& from, const std::string& to) {
    bool changed = false;
    for (auto& [full_name, pack] : installed_as)
        if (pack == from) {
            pack = to;
            changed = true;
        }
    return changed;
}
} // namespace dingosdk::anim_studio::market
