#include "Extension/AnimationStudio/library_navigation.h"
#include <iostream>
#include <stdexcept>
using namespace dingosdk::anim_studio;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
    try {
        LibraryNavigation route;
        route.select("Specials"); route.collection.pack = "Street"; route.collection.scroll = 170;
        route.add_to("Specials", "Boned");
        check(route.adding() && route.adding_to == "Specials" && route.browse().pack == "Boned", "The picker has an explicit, fixed destination");
        route.picker.query[0] = 'k'; route.picker.expanded.insert("Kickflip"); route.picker.scroll = 143;
        route.picker.pack_list_scroll = 271;
        const auto bookmark = route;
        route.show_page(1); route.collection.pack.clear(); route.select("Another playlist");
        route = bookmark;
        check(route == bookmark && route.picker.scroll == 143 && route.picker.expanded.contains("Kickflip"),
              "A preview returns to its exact picker, playlist, source, search, groups and scroll position");
        check(route.back() && route.adding() && route.picker.pack.empty(), "Back inside a picker pack returns to the pack list");
        check(route.picker.scroll == 271, "Back restores the pack-list position rather than its detail scroll");
        check(route.back() && !route.adding() && route.selected == "Specials", "The next Back returns to the playlist contents");
        route.show_page(1);
        check(route.browse().pack == "Street" && route.browse().scroll == 170, "Collection browsing has its own position independent of the picker");
        route.modifier_setup = true; route.settings = true; route.confirmation = "clear:Specials";
        check(route.back() && route.confirmation.empty() && route.modifier_setup, "Confirmation closes before underlying navigation");
        check(route.back() && !route.modifier_setup && route.settings, "Modifier help closes as its own page");
        check(route.back() && !route.settings, "Playlist settings close before leaving the Library");
        route.add_to("Specials"); route.select("Everyday");
        check(!route.adding() && route.selected == "Everyday", "Changing playlists cannot keep an invisible previous add destination");
        route.add_to(""); check(!route.adding(), "An empty destination never opens an inert picker");
        route.modifier_setup = route.choosing_modifier_playlist = true; route.show_page(1);
        check(!route.modifier_setup && !route.choosing_modifier_playlist,
              "Switching sections closes the nested setup chooser, so Back cannot target an invisible page");
        std::cout << "Library routes, preview bookmarks and Back navigation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
