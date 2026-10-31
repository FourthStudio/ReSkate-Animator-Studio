#pragma once
#include <array>
#include <set>
#include <string>

namespace dingosdk::anim_studio {
struct LibraryBrowseState {
    int source{};
    std::string pack, seen_query;
    std::array<char, 64> query{};
    std::set<std::string> expanded;
    float scroll{};
    float pack_list_scroll{};
    bool expand_all{};
    bool operator==(const LibraryBrowseState&) const = default;
};
// Editing a playlist and browsing the collection are distinct routes. Preview bookmarks this
// value, so returning does not infer an old pack page from the animation's source pack.
struct LibraryNavigation {
    int page{}; // 0 playlists, 1 collection
    std::string selected, adding_to, confirmation;
    bool modifier_setup{}, settings{}, creating{};
    bool choosing_modifier_playlist{};
    std::array<char, 64> new_name{};
    LibraryBrowseState collection, picker;
    std::array<char, 64> content_query{};
    std::string seen_content_query;
    std::set<std::string> open_packs;
    float playlists_scroll{}, content_scroll{}, settings_scroll{};
    float modifier_lists_scroll{};
    LibraryBrowseState& browse() { return adding_to.empty() ? collection : picker; }
    const LibraryBrowseState& browse() const { return adding_to.empty() ? collection : picker; }
    bool adding() const { return !adding_to.empty(); }
    void select(const std::string& name) {
        if (selected == name) return;
        selected = name;
        adding_to.clear(); confirmation.clear(); settings = false;
        content_query.fill(0); open_packs.clear(); content_scroll = 0;
    }
    void show_page(int next) {
        page = next;
        adding_to.clear(); confirmation.clear(); modifier_setup = settings = choosing_modifier_playlist = false;
    }
    void add_to(const std::string& name, const std::string& pack = {}) {
        if (name.empty()) return;
        select(name); page = 0; adding_to = name;
        modifier_setup = settings = choosing_modifier_playlist = false; confirmation.clear();
        if (!pack.empty() && picker.pack != pack) {
            picker.source = 0; picker.pack = pack; picker.scroll = 0;
            picker.expanded.clear(); picker.query.fill(0); picker.seen_query.clear();
        }
    }
    bool back() {
        if (creating) { creating = false; return true; }
        if (!confirmation.empty()) { confirmation.clear(); return true; }
        if (choosing_modifier_playlist) { choosing_modifier_playlist = false; return true; }
        if (modifier_setup) { modifier_setup = false; return true; }
        if (settings) { settings = false; return true; }
        auto& current = browse();
        if ((adding() || page == 1) && !current.pack.empty()) {
            current.pack.clear(); current.scroll = current.pack_list_scroll; return true;
        }
        if (adding()) { adding_to.clear(); return true; }
        return false;
    }
    bool operator==(const LibraryNavigation&) const = default;
};
}
