#include "Extension/AnimationStudio/trick_browser.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// TRICKS > MY TRICKS as a folder: its kinds, filters, order, groups, rows and choosing, on made-up libraries.
using namespace dingosdk::anim_studio;
using namespace dingosdk::anim_studio::trick_browser;
namespace {
void check(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
Item item(std::string name, std::string trick, Type type, int pop = 0, int stance = 0) {
    Item i;
    i.name = name;
    i.shown = name;
    i.trick = trick;
    i.trick_label = trick;
    i.type = type;
    i.pop = pop;
    i.stance = stance;
    i.key = native_search_key(name + " " + trick);
    return i;
}
std::vector<std::string> names_of(const std::vector<Item>& items, const std::vector<std::size_t>& at) {
    std::vector<std::string> out;
    for (const auto i : at) out.push_back(items[i].name);
    return out;
}

void kinds_come_from_the_catalog_and_the_name() {
    check(type_of("kickflip", "Flip tricks") == Type::flip, "A kickflip is not a flip");
    check(type_of("n_heelflip", "Nollie tricks") == Type::flip, "A nollie heelflip is not a flip");
    check(type_of("popshuvit", "Flip tricks") == Type::shuv, "The catalog files shuv-its under flips: the name must say shuv");
    check(type_of("n_fs360popshuvit", "Nollie tricks") == Type::shuv, "A nollie 360 shuv is not a shuv");
    check(type_of("ollie", "Flip tricks") == Type::other && type_of("nollie", "Nollie tricks") == Type::other, "An ollie is not a flip");
    check(type_of("native:x_5050_loop", "Grinds") == Type::grind && type_of("native:x_board", "Slides") == Type::grind, "Grinds and slides go together");
    check(type_of("native:x", "Manuals") == Type::manual, "A manual is not a manual");
    check(type_of("native:x", "Grabs") == Type::grab, "A grab is not a grab");
    check(type_of("native:x_casper", "Old school") == Type::other && type_of("native:x_flip_invert", "Inverts") == Type::other,
          "A kind of its own is OTHER, whatever its name has in it");
    check(type_of("native:x", "Fingerflips") == Type::flip && type_of("native:x", "Flip variations") == Type::flip, "Fingerflips are flips");
    // With no kind, the name: what the overlay's kind_of guesses for a trick the catalog does not have.
    check(type_of("bs_crook", "") == Type::grind && type_of("melon_grab", "") == Type::grab && type_of("hardflip", "") == Type::flip &&
              type_of("nose_manual", "") == Type::manual && type_of("strange", "") == Type::other,
          "A trick with no kind is not guessed from its name");
    check(pop_of("") == 0 && pop_of("low") == 1 && pop_of("mid") == 2 && pop_of("high") == 3, "Pops are not numbered any, low, mid, high");
}

void filters_take_any_of_a_kind_and_all_of_the_kinds() {
    std::vector<Item> items{item("A", "kickflip", Type::flip, 1), item("B", "kickflip", Type::flip, 2, 1), item("C", "popshuvit", Type::shuv, 3),
                            item("D", "native:grind", Type::grind, -1, 2)};
    items[0].packs = {"Street"};
    items[1].packs = {"Street", "Park"};
    items[2].maker = "norms";
    items[2].playlists = {"Everyday"};
    items[3].locked = items[3].from_pack = true;
    Filter f;
    check(f.active() == 0, "No filter set counts as one");
    f.types = (1u << static_cast<unsigned>(Type::flip)) | (1u << static_cast<unsigned>(Type::shuv));
    check(passes(items[0], f) && passes(items[2], f) && !passes(items[3], f), "Two kinds ticked do not show either");
    f.pops = 1u << 2;
    check(!passes(items[0], f) && passes(items[1], f) && !passes(items[3], f), "A pop filter lets other pops (or a piece with none) through");
    f = {};
    f.packs = {"Park"};
    check(!passes(items[0], f) && passes(items[1], f), "A trick in two packs is not found by its second");
    f.packs = {std::string{}};
    check(!passes(items[0], f) && passes(items[2], f), "IN NO PACK does not find those in none");
    f = {};
    f.playlists = 1;
    check(passes(items[2], f) && !passes(items[0], f), "IN A PLAYLIST does not find those in one");
    f.playlists = 2;
    check(!passes(items[2], f) && passes(items[0], f), "IN NONE does not find those in none");
    f = {};
    f.locks = 1;
    check(passes(items[3], f) && !passes(items[0], f), "LOCKED shows tricks that can be edited");
    f.locks = 2;
    check(!passes(items[3], f), "EDITABLE shows a locked trick");
    f = {};
    f.makers = {"norms"};
    check(passes(items[2], f) && !passes(items[0], f), "A maker filter finds another maker's");
    f = {};
    f.source = Source::own;
    check(passes(items[0], f) && !passes(items[3], f), "MY TRICKS shows a pack's tricks");
    f.source = Source::packs;
    check(!passes(items[0], f) && passes(items[3], f), "FROM PACKS shows your own");
    f = {};
    f.terms = native_search_terms("KICK flip");
    check(passes(items[0], f) && !passes(items[2], f), "A search for two words wants both, any case");
    f.stances = 1u << 1;
    check(passes(items[1], f) && !passes(items[0], f), "FAKIE shows a trick in its own stance");
    check(f.active() == 1, "The search counted as a filter");
}

void counts_say_what_each_choice_would_show() {
    std::vector<Item> items{item("A", "kickflip", Type::flip, 1), item("B", "kickflip", Type::flip, 2), item("C", "popshuvit", Type::shuv, 2),
                            item("D", "native:grind", Type::grind, -1)};
    items[0].packs = {"Street"};
    items[3].from_pack = true;
    Filter f;
    f.types = 1u << static_cast<unsigned>(Type::flip);
    f.pops = 1u << 2;
    const auto c = count(items, f);
    check(c.total == 4 && c.shown == 1, "Shown or total is wrong");
    // The kinds are counted with the pop filter only: MID has a flip (B) and a shuv (C).
    check(c.types[static_cast<std::size_t>(Type::flip)] == 1 && c.types[static_cast<std::size_t>(Type::shuv)] == 1 &&
              c.types[static_cast<std::size_t>(Type::grind)] == 0,
          "A kind's count is not what ticking it would show");
    // The pops with the kind filter only: the flips are LOW (A) and MID (B); the piece has no pop to count.
    check(c.pops[1] == 1 && c.pops[2] == 1 && c.pops[0] == 0, "A pop's count is not what ticking it would show");
    check(c.packs.contains("Street") && c.packs.contains(std::string{}) && c.packs.at("Street") == 0 && c.packs.at(std::string{}) == 1,
          "Every pack is listed (with what it would show), and IN NO PACK too");
    Filter mine;
    mine.source = Source::own;
    check(count(items, mine).total == 3, "The total counts the other source's tricks");
}

void order_is_natural_and_steady() {
    check(compare_text("Kickflip 2", "Kickflip 10") < 0 && compare_text("kickflip", "Kickflip") == 0 && compare_text("a", "B") < 0,
          "Names are not ordered as a person orders them");
    check(compare_text("Trick 007", "Trick 7") == 0 && compare_text("Trick 8", "Trick 07") > 0, "Leading noughts change a number");
    std::vector<Item> items{item("Kickflip 10", "kickflip", Type::flip), item("kickflip 2", "kickflip", Type::flip),
                            item("Heelflip", "heelflip", Type::flip)};
    items[0].modified = 30;
    items[1].modified = 10;
    items[2].modified = 20;
    auto by_name = arrange(items, {}, {Column::name, false}, GroupBy::none);
    check(by_name.size() == 1 && by_name[0].key.empty() && names_of(items, by_name[0].items) == std::vector<std::string>{"Heelflip", "kickflip 2", "Kickflip 10"},
          "Sorted by name in one group without a heading");
    auto newest = arrange(items, {}, {Column::modified, true}, GroupBy::none);
    check(names_of(items, newest[0].items) == std::vector<std::string>{"Kickflip 10", "Heelflip", "kickflip 2"}, "Newest first is not newest first");
    // Ties go by name, so the same library always comes out in the same order.
    items[2].modified = 30;
    newest = arrange(items, {}, {Column::modified, true}, GroupBy::none);
    check(names_of(items, newest[0].items) == std::vector<std::string>{"Heelflip", "Kickflip 10", "kickflip 2"}, "A tie is not broken by name");
    // Those in no pack come after those in one, either way round.
    items[0].packs = {"Zoo"};
    items[2].packs = {"Alpha"};
    for (const bool down : {false, true}) {
        const auto by_pack = arrange(items, {}, {Column::pack, down}, GroupBy::none);
        check(items[by_pack[0].items.back()].packs.empty(), "A trick in no pack is not last");
        check(items[by_pack[0].items.front()].packs.front() == (down ? "Zoo" : "Alpha"), "The packs are not in order");
    }
}

void groups_are_tricks_kinds_or_packs() {
    std::vector<Item> items{item("K1", "kickflip", Type::flip), item("H1", "heelflip", Type::flip), item("K2", "kickflip", Type::flip),
                            item("S1", "popshuvit", Type::shuv), item("Apart", "kickflip", Type::flip)};
    items[0].trick_label = items[2].trick_label = items[4].trick_label = "Kickflip";
    items[1].trick_label = "Heelflip";
    items[3].trick_label = "Pop Shuv-it";
    items[4].apart = true;
    items[0].packs = {"Street", "Park"};
    items[3].packs = {"Park"};
    const auto by_trick = arrange(items, {}, {}, GroupBy::trick);
    check(by_trick.size() == 4 && by_trick[0].key == "apart" && by_trick[1].title == "Heelflip" && by_trick[2].title == "Kickflip" &&
              by_trick[3].title == "Pop Shuv-it",
          "Grouped by trick: ON THEIR OWN first, then the tricks by name");
    check(names_of(items, by_trick[2].items) == std::vector<std::string>{"K1", "K2"}, "A trick's group lost a save, or took the one kept apart");
    const auto turned = arrange(items, {}, {Column::trick, true}, GroupBy::trick);
    check(turned[0].key == "apart" && turned[1].title == "Pop Shuv-it" && turned[3].title == "Heelflip",
          "Sorted Z to A by trick, the trick groups do not turn round (ON THEIR OWN stays first)");
    const auto by_type = arrange(items, {}, {}, GroupBy::type);
    check(by_type.size() == 2 && by_type[0].title == std::string("FLIPS") && by_type[1].title == std::string("SHUVS"), "Grouped by kind, in the kinds' order");
    const auto by_pack = arrange(items, {}, {}, GroupBy::pack);
    check(by_pack.size() == 3 && by_pack[0].title == "Park" && by_pack[1].title == "Street" && by_pack[2].title == "IN NO PACK",
          "Grouped by pack: by name, IN NO PACK last");
    check(names_of(items, by_pack[0].items) == std::vector<std::string>{"K1", "S1"} && names_of(items, by_pack[1].items) == std::vector<std::string>{"K1"},
          "A trick in two packs is not in both groups");
    Filter shuvs;
    shuvs.types = 1u << static_cast<unsigned>(Type::shuv);
    check(arrange(items, shuvs, {}, GroupBy::trick).size() == 1, "A group with nothing that passes the filter is shown");
}

void rows_lay_out_headings_lists_and_icons() {
    std::vector<Group> groups{{"a", "A", {0, 1, 2, 3, 4}}, {"b", "B", {5, 6}}};
    Metrics m;
    m.header_h = 30;
    m.row_h = 40;
    m.tile_h = 100;
    m.gap = 5;
    m.group_gap = 10;
    m.columns = 1;
    const auto list = lay_out(groups, {}, ViewMode::list, m);
    check(list.rows.size() == 9 && list.rows[0].header && !list.rows[1].header && list.rows[1].top == 35, "A list is a heading and a row an item");
    check(list.rows[6].header && list.rows[6].top == 35 + 5 * 45 + 10, "The next group does not start below the last row and the gap");
    // On a wide screen a LIST goes two (or more) across; DETAILS stays a table, an item a row.
    m.columns = 2;
    const auto wide = lay_out(groups, {}, ViewMode::list, m);
    check(wide.rows.size() == 6 && wide.rows[1].count == 2 && wide.rows[3].count == 1 && wide.rows[1].height == 40, "A wide LIST is not laid two across");
    check(lay_out(groups, {}, ViewMode::details, m).rows.size() == 9, "DETAILS is laid more than one across");
    m.columns = 3;
    const auto icons = lay_out(groups, {}, ViewMode::icons, m);
    check(icons.rows.size() == 5 && icons.rows[1].count == 3 && icons.rows[2].count == 2 && icons.rows[2].first == 3 && icons.rows[4].count == 2,
          "Icons are not laid three across");
    const auto folded = lay_out(groups, {"a"}, ViewMode::list, m);
    check(folded.rows.size() == 4 && folded.rows[1].header && folded.rows[1].group == 1, "A folded group still shows its items");
    check(shown_order(groups, {"a"}) == std::vector<std::size_t>{5, 6}, "A folded group's items are in a Shift + click's range");
    const std::vector<Group> plain{{"", "", {0, 1, 2}}};
    const auto flat = lay_out(plain, {}, ViewMode::details, m);
    check(flat.rows.size() == 3 && !flat.rows[0].header && flat.rows[0].top == 0 && flat.height == 3 * 40 + 2 * 5,
          "A list that is not grouped has a heading, or the wrong height");
}

void only_the_rows_on_screen_are_drawn() {
    std::vector<Group> groups{{"", "", {}}};
    for (std::size_t i = 0; i < 600; ++i) groups[0].items.push_back(i);
    Metrics m;
    m.row_h = 40;
    m.gap = 0;
    const auto l = lay_out(groups, {}, ViewMode::details, m);
    const auto [from, to] = visible(l, 4000, 4400);
    check(from == 100 && to == 110, "The rows between the scrolled top and bottom are not the ones drawn");
    const auto [mfrom, mto] = visible(l, 4000, 4400, 2);
    check(mfrom == 98 && mto == 112, "The margin either side is not there");
    const auto [efrom, eto] = visible(l, 30000, 30400, 2);
    check(efrom <= eto && eto == 600, "Scrolled past the end draws beyond the rows");
    check(visible(Layout{}, 0, 100).first == 0 && visible(Layout{}, 0, 100).second == 0, "An empty list draws something");
}

void a_click_chooses_one_ctrl_adds_shift_runs() {
    const std::vector<std::string> order{"a", "b", "c", "d", "e"};
    Selection s;
    click(s, order, "b", false, false);
    check(s.names == std::set<std::string>{"b"} && s.anchor == "b", "A click does not choose that one alone");
    click(s, order, "d", true, false);
    check(s.names == std::set<std::string>{"b", "d"} && s.anchor == "d", "Ctrl + click does not add it");
    click(s, order, "b", true, false);
    check(s.names == std::set<std::string>{"d"} && s.anchor == "b", "Ctrl + click on a chosen one does not take it away");
    click(s, order, "e", false, true);
    check(s.names == std::set<std::string>{"b", "c", "d", "e"} && s.anchor == "b", "Shift + click does not run from the anchor");
    click(s, order, "a", false, true);
    check(s.names == std::set<std::string>{"a", "b"}, "Shift + click again does not run from the same anchor, alone");
    click(s, order, "e", false, false);
    click(s, order, "c", true, true);
    check(s.names == std::set<std::string>{"c", "d", "e"}, "Ctrl + Shift + click does not add the run");
    Selection lost;
    lost.anchor = "gone";
    click(lost, order, "c", false, true);
    check(lost.names == std::set<std::string>{"c"} && lost.anchor == "c", "Shift + click with no anchor shown does not choose the one clicked");
    keep_existing(s, [](const std::string& n) { return n != "d"; });
    check(s.names == std::set<std::string>{"c", "e"}, "A deleted trick stays chosen");
}

void the_table_gives_up_columns_when_narrow() {
    const auto wide = fit_columns(2000, 1);
    check(wide.size() == column_count, "A wide table leaves a column out");
    float sum = 0;
    for (const auto& [c, w] : wide) sum += w;
    check(sum > 1999 && sum < 2001, "The columns do not fill the table");
    const auto narrow = fit_columns(700, 1);
    check(narrow.front().first == Column::name && narrow.size() < column_count, "A narrow table keeps every column or loses NAME");
    for (const auto& [c, w] : narrow) check(c != Column::maker, "MADE BY, the least needed, stays when others go");
    float min_sum = 0;
    for (const auto& [c, w] : narrow) min_sum += w;
    check(min_sum <= 700.5f, "What is left does not fit");
    check(fit_columns(10, 1).size() == 1, "NAME is given up");
    const auto scaled = fit_columns(1400, 2);
    check(scaled.size() < fit_columns(1400, 1).size(), "The columns do not scale with the screen");
}

void a_mark_changes_with_what_it_is_made_of() {
    Mark a, b;
    a.add("kick");
    a.add(std::uint64_t{3});
    b.add("kick");
    b.add(std::uint64_t{3});
    check(a.value == b.value, "The same makes another mark");
    Mark c;
    c.add("kic");
    c.add("k");
    check(c.value != a.value, "Words split differently make the same mark");
}
} // namespace

int main() {
    try {
        kinds_come_from_the_catalog_and_the_name();
        filters_take_any_of_a_kind_and_all_of_the_kinds();
        counts_say_what_each_choice_would_show();
        order_is_natural_and_steady();
        groups_are_tricks_kinds_or_packs();
        rows_lay_out_headings_lists_and_icons();
        only_the_rows_on_screen_are_drawn();
        a_click_chooses_one_ctrl_adds_shift_runs();
        the_table_gives_up_columns_when_narrow();
        a_mark_changes_with_what_it_is_made_of();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "MY TRICKS browser tests passed\n";
}
