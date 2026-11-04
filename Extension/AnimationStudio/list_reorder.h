#pragma once
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

// Putting one item of an ordered list somewhere else in it, as a card taken out of a hand and pushed back in at another
// place: the ones between shift by one, nothing else moves. (The LIBRARY moved a playlist's entries by swapping them with
// a neighbour, one place a press, so taking the 22nd to 4th place took 18 presses and every swap disturbed the order of
// the ones passed.) Pure, so it is tested on its own; the LIBRARY's drag and its pad and keyboard moves are built on it.
namespace dingosdk::anim_studio {
// The item at `from` ends up at `to` (both places in the list as it is before the move). False when nothing changes or
// either place is outside the list.
template <class T>
bool move_to_index(std::vector<T>& items, std::size_t from, std::size_t to) {
    if (from >= items.size() || to >= items.size() || from == to) return false;
    const auto at = [&](std::size_t i) { return items.begin() + static_cast<std::ptrdiff_t>(i); };
    if (from < to) std::rotate(at(from), at(from + 1), at(to + 1));
    else std::rotate(at(to), at(from), at(from + 1));
    return true;
}
// A drop marker sits in a gap between rows: gap 0 above the first, gap n under the last. Dropping the item from `from`
// into gap `gap` puts it at this place (the gaps just above and just under it leave it where it is).
inline std::size_t target_for_gap(std::size_t from, std::size_t gap) { return gap > from ? gap - 1 : gap; }
// And back: the gap the marker is drawn in for an item carried from `from` to `to`.
inline std::size_t gap_for_target(std::size_t from, std::size_t to) { return to > from ? to + 1 : to; }
// The gap under a pointer at height `y`, for rows whose own heights are `tops` to `bottoms` (in order, top to bottom): the
// gap above the first row whose middle is below the pointer. Rows may be of any height (a pack shown open is taller).
inline std::size_t gap_at(std::span<const float> tops, std::span<const float> bottoms, float y) {
    const std::size_t n = std::min(tops.size(), bottoms.size());
    for (std::size_t i = 0; i < n; ++i)
        if (y < (tops[i] + bottoms[i]) * 0.5f) return i;
    return n;
}
// A pad or a key moving a carried item: one place up (-1) or down (1) a press, never past either end.
inline std::size_t step_target(std::size_t to, int by, std::size_t count) {
    if (count == 0) return 0;
    const auto moved = static_cast<std::ptrdiff_t>(to) + by;
    return static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(moved, 0, static_cast<std::ptrdiff_t>(count) - 1));
}
// Scrolling a list while something is dragged near its top or bottom edge: 0 away from the edges, up to -1 (up) or 1
// (down) at and past them, growing over `zone` so it can be slowed by backing off. Multiplied by a speed by the caller.
inline float edge_scroll(float y, float top, float bottom, float zone) {
    if (zone <= 0 || bottom - top < 2 * zone) return 0;
    if (y < top + zone) return -std::min(1.0f, (top + zone - y) / zone);
    if (y > bottom - zone) return std::min(1.0f, (y - (bottom - zone)) / zone);
    return 0;
}
} // namespace dingosdk::anim_studio
