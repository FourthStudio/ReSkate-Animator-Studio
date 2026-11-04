#pragma once
#include "native_asset_names.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// TRICKS > MY TRICKS as a folder: every saved trick an item with what it is (its trick and kind, stance, pop, packs, maker,
// when it was saved, the playlists it is in), and the pure parts of showing hundreds of them as icons, a list or a table -
// which pass the filters, in what order, in which groups, laid out in rows, which rows are on screen, and what a click
// chooses. The overlay builds the items when the library changes and keeps what comes of these until something they read
// changes, so none of it runs every frame for every trick (with 500 saved, rebuilding each frame was what made the old
// page heavy).
namespace dingosdk::anim_studio::trick_browser {

// ---- what an item is --------------------------------------------------------------------------------
enum class Type : std::uint8_t { flip, shuv, grind, manual, grab, other };
inline constexpr std::size_t type_count = 6;
inline constexpr std::array<const char*, type_count> type_names{"FLIPS", "SHUVS", "GRINDS & SLIDES", "MANUALS", "GRABS", "OTHER"};
inline constexpr std::array<const char*, 4> stance_names{"NORMAL", "FAKIE", "SWITCH", "SWITCH FAKIE"};
inline constexpr std::array<const char*, 4> pop_names{"ANY", "LOW", "MID", "HIGH"};

namespace detail {
inline bool has(std::string_view key, std::string_view word) { return key.find(word) != std::string_view::npos; }
} // namespace detail

// The kind of trick, for the filter and the icons. `kind` is the list the game's catalog files it under ("Flip tricks",
// "Nollie tricks", "Grinds", "Slides", "Grabs", "Manuals", ...: the overlay's kind_of); its name decides where the kind does
// not (the catalog's Flip tricks hold the shuv-its and the ollie too) or where there is no kind.
inline Type type_of(std::string_view trick, std::string_view kind) {
    using detail::has;
    const auto t = native_search_key(trick), k = native_search_key(kind);
    if (has(t, "shuv") || has(t, "shove") || k == "shoveits") return Type::shuv;
    if (k == "grinds" || k == "slides") return Type::grind;
    if (k == "manuals") return Type::manual;
    if (k == "grabs") return Type::grab;
    if (t == "ollie" || t == "nollie" || k == "ollies") return Type::other;
    if (k == "fliptricks" || k == "nollietricks" || k == "fingerflips" || k == "flipvariations") return Type::flip;
    if (!k.empty() && k != "other") return Type::other; // a kind of its own: inverts, old school, powerslides
    // No kind: a good guess from the name.
    if (has(t, "flip") || has(t, "impossible") || has(t, "varial")) return Type::flip;
    if (has(t, "grind") || has(t, "slide") || has(t, "5050") || has(t, "crook") || has(t, "smith") || has(t, "feeble") ||
        has(t, "salad") || has(t, "suski") || has(t, "willy") || has(t, "blunt") || has(t, "nosegrind"))
        return Type::grind;
    if (has(t, "manual")) return Type::manual;
    if (has(t, "grab") || has(t, "indy") || has(t, "melon") || has(t, "mute") || has(t, "stalefish")) return Type::grab;
    return Type::other;
}
// A saved trick's pop as its file says it: 0 any (""), 1 low, 2 mid, 3 high.
inline int pop_of(std::string_view pop) { return pop == "low" ? 1 : pop == "mid" ? 2 : pop == "high" ? 3 : 0; }

struct Item {
    std::string name;                // its file's name: what every action is given
    std::string shown;               // the name it is shown by (its label, else its name)
    std::string trick, trick_label;  // the game's trick it plays for, and the game's name for it
    Type type{Type::other};
    int stance{};                    // 0 your own, 1 fakie, 2 switch, 3 switch fakie (stance_version)
    int pop{};                       // -1 none (a piece of the game's animation has no pop), 0 any, 1 low, 2 mid, 3 high
    std::vector<std::string> packs;  // the packs it is in
    std::string maker;               // who made it ("" not said)
    std::int64_t modified{};         // when its file was last written (the file clock's count)
    std::vector<std::string> playlists; // the playlists it is in
    bool plays{};                    // in the playlist that plays now
    bool locked{};                   // its maker's only: it plays and previews, but is never opened to edit
    bool from_pack{};                // came in a pack (FROM PACKS); else made here (MY TRICKS)
    bool apart{};                    // taken out of its trick's group (OUT OF ITS GROUP)
    std::string key;                 // everything a search looks through (native_search_key)
};

// ---- the filters --------------------------------------------------------------------------------------
enum class Source : std::uint8_t { all, own, packs };
struct Filter {
    Source source{Source::all};
    unsigned types{}, stances{}, pops{}; // a bit a choice (1 << Type, 1 << stance, 1 << pop); none set: any
    std::set<std::string> packs, makers; // any of these ("" in no pack / no maker named); empty: any
    int playlists{};                     // 0 either, 1 in a playlist, 2 in none
    int locks{};                         // 0 either, 1 locked, 2 can be edited
    std::vector<std::string> terms;      // the search (native_search_terms): each in the item's key
    // How many filters are set, the search and the source not counted: the FILTER tab's number.
    int active() const {
        return (types != 0) + (stances != 0) + (pops != 0) + !packs.empty() + !makers.empty() + (playlists != 0) + (locks != 0);
    }
    void clear() {
        types = stances = pops = 0;
        packs.clear();
        makers.clear();
        playlists = locks = 0;
    }
    bool operator==(const Filter&) const = default;
};
// One kind of filter, to leave out when counting what each of its own choices would show.
enum class Facet : std::uint8_t { none, type, stance, pop, pack, maker, playlist, lock };
inline bool passes(const Item& i, const Filter& f, Facet skip = Facet::none) {
    if (f.source == Source::own && i.from_pack) return false;
    if (f.source == Source::packs && !i.from_pack) return false;
    if (!native_search_matches(i.key, f.terms)) return false;
    if (skip != Facet::type && f.types && !(f.types & (1u << static_cast<unsigned>(i.type)))) return false;
    if (skip != Facet::stance && f.stances && !(f.stances & (1u << static_cast<unsigned>(std::clamp(i.stance, 0, 3))))) return false;
    if (skip != Facet::pop && f.pops && (i.pop < 0 || !(f.pops & (1u << static_cast<unsigned>(i.pop))))) return false;
    if (skip != Facet::pack && !f.packs.empty()) {
        const bool any = i.packs.empty() ? f.packs.contains(std::string{})
                                         : std::any_of(i.packs.begin(), i.packs.end(), [&](const std::string& p) { return f.packs.contains(p); });
        if (!any) return false;
    }
    if (skip != Facet::maker && !f.makers.empty() && !f.makers.contains(i.maker)) return false;
    if (skip != Facet::playlist && f.playlists == 1 && i.playlists.empty()) return false;
    if (skip != Facet::playlist && f.playlists == 2 && !i.playlists.empty()) return false;
    if (skip != Facet::lock && f.locks == 1 && !i.locked) return false;
    if (skip != Facet::lock && f.locks == 2 && i.locked) return false;
    return true;
}
// How many each choice of the filter panel would show with the other filters as they are (a choice that shows nothing
// says so), and how many pass them all out of how many there are.
struct Counts {
    std::array<std::size_t, type_count> types{};
    std::array<std::size_t, 4> stances{}, pops{};
    std::map<std::string, std::size_t> packs, makers; // every pack and maker there is, "" none
    std::size_t in_playlist{}, in_none{}, locked{}, editable{};
    std::size_t shown{}, total{};
};
inline Counts count(const std::vector<Item>& items, const Filter& f) {
    Counts c;
    Filter all_of_it;
    all_of_it.source = f.source;
    for (const auto& i : items) {
        if (!passes(i, all_of_it)) continue;
        ++c.total;
        if (i.packs.empty()) c.packs.try_emplace(std::string{}, 0);
        for (const auto& p : i.packs) c.packs.try_emplace(p, 0);
        c.makers.try_emplace(i.maker, 0);
        if (passes(i, f)) ++c.shown;
        if (passes(i, f, Facet::type)) ++c.types[static_cast<std::size_t>(i.type)];
        if (passes(i, f, Facet::stance)) ++c.stances[static_cast<std::size_t>(std::clamp(i.stance, 0, 3))];
        if (i.pop >= 0 && passes(i, f, Facet::pop)) ++c.pops[static_cast<std::size_t>(std::clamp(i.pop, 0, 3))];
        if (passes(i, f, Facet::pack)) {
            if (i.packs.empty()) ++c.packs[std::string{}];
            for (const auto& p : i.packs) ++c.packs[p];
        }
        if (passes(i, f, Facet::maker)) ++c.makers[i.maker];
        if (passes(i, f, Facet::playlist)) ++(i.playlists.empty() ? c.in_none : c.in_playlist);
        if (passes(i, f, Facet::lock)) ++(i.locked ? c.locked : c.editable);
    }
    return c;
}

// ---- the order --------------------------------------------------------------------------------------
enum class Column : std::uint8_t { name, trick, stance, pop, pack, maker, modified, playlists };
inline constexpr std::size_t column_count = 8;
inline constexpr std::array<const char*, column_count> column_names{"NAME", "TRICK", "STANCE", "POP", "PACK", "MADE BY", "MODIFIED", "IN PLAYLISTS"};
struct Sort {
    Column by{Column::name};
    bool descending{};
    bool operator==(const Sort&) const = default;
};
// Words as a person orders them: case does not count, and a number counts as a number ("Kickflip 2" before "Kickflip 10").
inline int compare_text(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            std::size_t ei = i, ej = j;
            while (ei < a.size() && a[ei] == '0') ++ei; // leading noughts do not make it bigger
            while (ej < b.size() && b[ej] == '0') ++ej;
            std::size_t ni = ei, nj = ej;
            while (ni < a.size() && digit(a[ni])) ++ni;
            while (nj < b.size() && digit(b[nj])) ++nj;
            if (ni - ei != nj - ej) return ni - ei < nj - ej ? -1 : 1;
            for (; ei < ni; ++ei, ++ej)
                if (a[ei] != b[ej]) return a[ei] < b[ej] ? -1 : 1;
            i = ni, j = nj;
            continue;
        }
        const char x = lower(a[i]), y = lower(b[j]);
        if (x != y) return x < y ? -1 : 1;
        ++i, ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}
inline int compare(const Item& a, const Item& b, Column by) {
    const auto three = [](auto x, auto y) { return x < y ? -1 : y < x ? 1 : 0; };
    switch (by) {
    case Column::name: return compare_text(a.shown, b.shown);
    case Column::trick: return compare_text(a.trick_label, b.trick_label);
    case Column::stance: return three(a.stance, b.stance);
    case Column::pop: return three(a.pop, b.pop);
    case Column::pack: return a.packs.empty() || b.packs.empty() ? 0 : compare_text(a.packs.front(), b.packs.front());
    case Column::maker: return compare_text(a.maker, b.maker);
    case Column::modified: return three(a.modified, b.modified);
    case Column::playlists:
        if (const int c = three(a.playlists.size(), b.playlists.size()); c != 0) return c;
        return a.playlists.empty() ? 0 : compare_text(a.playlists.front(), b.playlists.front());
    }
    return 0;
}
// Nothing to sort by in that column (no pack, no maker named, no playlist): those go last, either way round.
inline bool blank(const Item& i, Column by) {
    return (by == Column::pack && i.packs.empty()) || (by == Column::maker && i.maker.empty()) || (by == Column::playlists && i.playlists.empty());
}
// Ties go by name either way round, then by file, so the order never changes from one rebuild to the next.
inline bool before(const Item& a, const Item& b, Sort s) {
    if (blank(a, s.by) != blank(b, s.by)) return blank(b, s.by);
    int c = compare(a, b, s.by);
    if (s.descending) c = -c;
    if (c != 0) return c < 0;
    if (const int n = compare_text(a.shown, b.shown); n != 0) return n < 0;
    return a.name < b.name;
}

// ---- the groups -------------------------------------------------------------------------------------
enum class GroupBy : std::uint8_t { trick, type, pack, none };
struct Group {
    std::string key;   // what it is folded by ("" the one group of a list not grouped: it has no heading)
    std::string title; // its heading
    std::vector<std::size_t> items; // places in the items, in the order shown
};
// What passes the filter, in order, in its groups. By trick: a group a trick, those taken out of theirs first (ON THEIR OWN);
// by pack: a trick in two packs is in both groups, those in none last; the groups in the order of their names, turned
// round when the list is sorted by what they are grouped by.
inline std::vector<Group> arrange(const std::vector<Item>& items, const Filter& f, Sort s, GroupBy by) {
    std::vector<std::size_t> order;
    order.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
        if (passes(items[i], f)) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return before(items[a], items[b], s); });
    if (by == GroupBy::none) return {Group{{}, {}, std::move(order)}};
    std::vector<Group> groups;
    std::map<std::string, std::size_t> at; // a group's key: its place
    const auto into = [&](const std::string& key, const std::string& title, std::size_t item) {
        auto [found, fresh] = at.try_emplace(key, groups.size());
        if (fresh) groups.push_back(Group{key, title, {}});
        groups[found->second].items.push_back(item);
    };
    for (const auto i : order) {
        const auto& item = items[i];
        switch (by) {
        case GroupBy::trick:
            if (item.apart) into("apart", "ON THEIR OWN", i);
            else into("trick:" + item.trick, item.trick_label.empty() ? std::string("OTHER") : item.trick_label, i);
            break;
        case GroupBy::type: into("type:" + std::to_string(static_cast<int>(item.type)), type_names[static_cast<std::size_t>(item.type)], i); break;
        case GroupBy::pack:
            if (item.packs.empty()) into("pack:", "IN NO PACK", i);
            for (const auto& p : item.packs) into("pack:" + p, p, i);
            break;
        case GroupBy::none: break;
        }
    }
    const bool turned = s.descending && ((by == GroupBy::trick && s.by == Column::trick) || (by == GroupBy::pack && s.by == Column::pack));
    const auto last = [&](const Group& g) { return g.key == "pack:" ? 2 : g.key == "apart" ? 0 : 1; }; // ON THEIR OWN first, IN NO PACK last
    std::stable_sort(groups.begin(), groups.end(), [&](const Group& a, const Group& b) {
        if (last(a) != last(b)) return last(a) < last(b);
        int c = by == GroupBy::type ? compare_text(a.key, b.key) : compare_text(a.title, b.title);
        if (turned) c = -c;
        return c != 0 ? c < 0 : a.key < b.key;
    });
    return groups;
}

// ---- the rows ---------------------------------------------------------------------------------------
enum class ViewMode : std::uint8_t { icons, list, details };
struct Row {
    bool header{};                 // a group's heading, else items
    std::size_t group{};           // the group it is in
    std::size_t first{}, count{};  // items: from this place in its group, this many (a row of icons has more than one)
    float top{}, height{};
};
// How tall each row is (already scaled), and how many items go across: icons, and a LIST's rows on a wide screen (one
// long row each across 2000 units of a 21:9 screen left the name far from its tags); DETAILS is a table, one a row.
struct Metrics {
    float header_h{36}, row_h{40}, tile_h{150}, gap{4}, group_gap{10};
    std::size_t columns{1};
};
struct Layout {
    std::vector<Row> rows;
    float height{};
};
// Every row from the top: a heading a group (none for a list not grouped), its items under it unless it is folded.
inline Layout lay_out(const std::vector<Group>& groups, const std::set<std::string>& folded, ViewMode mode, const Metrics& m) {
    Layout out;
    float y = 0;
    const std::size_t across = mode == ViewMode::details ? 1 : std::max<std::size_t>(1, m.columns);
    const float item_h = mode == ViewMode::icons ? m.tile_h : m.row_h;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const auto& group = groups[g];
        const bool headed = !group.key.empty();
        if (headed) {
            out.rows.push_back(Row{true, g, 0, 0, y, m.header_h});
            y += m.header_h + m.gap;
        }
        if (headed && folded.contains(group.key)) {
            y += m.group_gap - m.gap;
            continue;
        }
        for (std::size_t i = 0; i < group.items.size(); i += across) {
            out.rows.push_back(Row{false, g, i, std::min(across, group.items.size() - i), y, item_h});
            y += item_h + m.gap;
        }
        if (headed) y += m.group_gap;
    }
    out.height = std::max(0.0f, y - m.gap);
    return out;
}
// The rows any part of which lies between `top` and `bottom` (the list's own height, scrolled), and `margin` more each
// side: those are drawn, the rest only counted.
inline std::pair<std::size_t, std::size_t> visible(const Layout& l, float top, float bottom, std::size_t margin = 0) {
    const auto first = std::partition_point(l.rows.begin(), l.rows.end(), [&](const Row& r) { return r.top + r.height <= top; });
    const auto end = std::partition_point(first, l.rows.end(), [&](const Row& r) { return r.top < bottom; });
    const auto from = static_cast<std::size_t>(first - l.rows.begin()), to = static_cast<std::size_t>(end - l.rows.begin());
    return {from > margin ? from - margin : 0, std::min(l.rows.size(), to + margin)};
}
// The items in the order they are shown, folded groups left out: what a Shift + click's range runs through.
inline std::vector<std::size_t> shown_order(const std::vector<Group>& groups, const std::set<std::string>& folded) {
    std::vector<std::size_t> out;
    for (const auto& g : groups)
        if (g.key.empty() || !folded.contains(g.key)) out.insert(out.end(), g.items.begin(), g.items.end());
    return out;
}

// ---- choosing -----------------------------------------------------------------------------------------
struct Selection {
    std::set<std::string> names;
    std::string anchor; // where a Shift + click's range starts
    bool operator==(const Selection&) const = default;
};
// A click on `name`, `order` being the names as shown. Alone it chooses that one; `add` (Ctrl, or SELECT on) adds it or
// takes it away; `range` (Shift) chooses everything from the anchor to it (with `add`, as well as what was chosen).
inline void click(Selection& s, const std::vector<std::string>& order, const std::string& name, bool add, bool range) {
    if (range && !s.anchor.empty()) {
        const auto from = std::find(order.begin(), order.end(), s.anchor), to = std::find(order.begin(), order.end(), name);
        if (from != order.end() && to != order.end()) {
            if (!add) s.names.clear();
            const auto [lo, hi] = std::minmax(from, to);
            s.names.insert(lo, hi + 1);
            return;
        }
    }
    if (add) {
        if (!s.names.erase(name)) s.names.insert(name);
    } else s.names = {name};
    s.anchor = name;
}
// What is chosen and no longer there (deleted, renamed) is not chosen.
template <class Exists>
void keep_existing(Selection& s, Exists&& exists) {
    std::erase_if(s.names, [&](const std::string& n) { return !exists(n); });
    if (!s.anchor.empty() && !exists(s.anchor)) s.anchor.clear();
}

// ---- the table's columns ------------------------------------------------------------------------------
struct ColumnSpec {
    Column column;
    float min_width, weight; // unscaled; spare room goes by weight
};
inline constexpr std::array<ColumnSpec, column_count> column_specs{{{Column::name, 220, 3.0f},
                                                                    {Column::trick, 150, 1.6f},
                                                                    {Column::stance, 104, 0.8f},
                                                                    {Column::pop, 70, 0.4f},
                                                                    {Column::pack, 140, 1.4f},
                                                                    {Column::maker, 120, 1.0f},
                                                                    {Column::modified, 126, 0.6f},
                                                                    {Column::playlists, 130, 1.2f}}};
// The order columns are given up in when the table is narrow (the drawer open, a small screen): the last first.
inline constexpr std::array<Column, column_count> column_priority{Column::name, Column::trick, Column::pop, Column::stance,
                                                                  Column::modified, Column::pack, Column::playlists, Column::maker};
// The columns that fit in `width` (scale `k`), each with its width, left to right: those least needed are left out until the
// rest fit, and the room over is shared out. NAME always stays.
inline std::vector<std::pair<Column, float>> fit_columns(float width, float k) {
    std::array<bool, column_count> on{};
    on.fill(true);
    const auto needed = [&] {
        float w = 0;
        for (const auto& s : column_specs)
            if (on[static_cast<std::size_t>(s.column)]) w += s.min_width * k;
        return w;
    };
    for (std::size_t drop = column_count; drop-- > 1 && needed() > width;) on[static_cast<std::size_t>(column_priority[drop])] = false;
    float weights = 0;
    for (const auto& s : column_specs)
        if (on[static_cast<std::size_t>(s.column)]) weights += s.weight;
    const float spare = std::max(0.0f, width - needed());
    std::vector<std::pair<Column, float>> out;
    for (const auto& s : column_specs)
        if (on[static_cast<std::size_t>(s.column)]) out.emplace_back(s.column, s.min_width * k + (weights > 0 ? spare * s.weight / weights : 0));
    return out;
}

// ---- keeping what was worked out ------------------------------------------------------------------------
// A running hash (FNV-1a) of what a cached result was made from: when it comes out the same, the result still stands.
struct Mark {
    std::uint64_t value{1469598103934665603ull};
    void add(std::uint64_t x) {
        for (int i = 0; i < 8; ++i) value = (value ^ ((x >> (i * 8)) & 0xff)) * 1099511628211ull;
    }
    void add(std::string_view text) {
        add(text.size());
        for (const unsigned char c : text) value = (value ^ c) * 1099511628211ull;
    }
};
} // namespace dingosdk::anim_studio::trick_browser
