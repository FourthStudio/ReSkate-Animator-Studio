#include "real_bails_model.h"
#include "real_bails_body.h"
#include "real_bails_limits.h"
#include "real_bails_read.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::real_bails::model {
namespace {
using namespace math;
constexpr float pi = 3.14159265f;
constexpr float deg = pi / 180.0f;
constexpr V3 world_up{0, 1, 0};
constexpr std::size_t joint_count = Muscles::joint_count;

// The joints in the stop table's order (real_bails_limits.h): from the pelvis outwards, a shoulder after its collarbone.
const std::array<limits::Limit, joint_count>& ranges() noexcept {
    static const auto table = limits::table();
    return table;
}
const std::array<std::size_t, part_count>& index_of() noexcept {
    static const auto index = [] {
        std::array<std::size_t, part_count> out{};
        for (std::size_t k = 0; k < joint_count; ++k) out[ranges()[k].child] = k;
        return out;
    }();
    return index;
}

enum Group : std::uint8_t { trunk, neck_group, arm_left, arm_right, leg_left, leg_right };
Group group_of(Part c) noexcept {
    switch (c) {
    case spine: case back1: case chest2: case chest3: return trunk;
    case neck: case head: return neck_group;
    case l_collar: case l_arm: case l_forearm: case l_hand: return arm_left;
    case r_collar: case r_arm: case r_forearm: case r_hand: return arm_right;
    case l_thigh: case l_shin: case l_foot: case l_toe: return leg_left;
    default: return leg_right;
    }
}
bool is_end(Part p) noexcept { return p == l_hand || p == r_hand || p == l_foot || p == r_foot || p == l_toe || p == r_toe; }
bool is_torso(Part p) noexcept {
    return p == pelvis || p == spine || p == back1 || p == chest2 || p == chest3 || p == neck || p == head || p == l_collar || p == r_collar;
}

float fin(float v, float lo, float hi, float otherwise) noexcept { return std::isfinite(v) ? std::clamp(v, lo, hi) : otherwise; }
float smooth(float x) noexcept {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}
float lerp(float a, float b, float f) noexcept { return a + (b - a) * f; }
V3 flat(const V3& v) noexcept { return {v[0], 0, v[2]}; }
// The rotation vector that turns axes `now` onto `want` (both world).
V3 turn_to(const M3& now, const M3& want) noexcept { return rotation_vector(multiply(want, transposed(now))); }
// A unit vector turned towards another by at most `most` radians.
V3 turn_towards(const V3& from, const V3& to, float most) noexcept {
    const float angle = std::acos(std::clamp(dot(from, to), -1.0f, 1.0f));
    if (angle <= most) return to;
    V3 axis = cross(from, to);
    if (length(axis) < 1e-5f) axis = cross(from, std::abs(from[1]) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0});
    return normalized(turned(from, normalized(axis), most), to);
}
// A hinge's bend from its child's axes in its frame's: the turn of the bone about the frame's z.
float bend_of(const M3& rel) noexcept { return std::atan2(rel[0][1], rel[0][0]); }

// What a joint's muscles do in one step: tone (a damper on the joint's own turning, `damp` per second of its inertia),
// or a goal the joint is turned towards (the arms' reflex, the pain reaction) with a stiffness and damping ratio.
struct Drive {
    float u{};            // activation, 0..1: its torque is at most u x its strength
    float damp{1};        // tone: how fast it takes the joint's own turning out, per second
    bool goal{};
    M3 want{identity3()}; // the goal: the child's axes in the joint's frame (a hinge: only its bend counts)
    float wn{3}, zeta{1};
};

// What is written keeps the body's momentum exactly: a hand or foot planted on the ground is not written, so its share of
// the turns is taken back out of the rest as one rigid motion (linear, and angular about their centre), which leaves
// every joint's own turn as it was.
void keep_momentum(const body::Mass& m, const std::array<bool, part_count>& written, std::array<V3, part_count>& dw,
                   std::array<V3, part_count>& dv) noexcept {
    float total = 0;
    V3 first{};
    for (std::size_t i = 1; i < part_count; ++i)
        if (written[i]) {
            total += body::shapes[i].mass;
            first = first + m.at[i] * body::shapes[i].mass;
        }
    if (total <= 0) return;
    const V3 centre = first * (1.0f / total);
    V3 linear{}, angular{};
    M3 inertia{};
    for (std::size_t i = 1; i < part_count; ++i) {
        if (!written[i]) continue;
        const float kg = body::shapes[i].mass;
        const V3 r = m.at[i] - centre;
        linear = linear + dv[i] * kg;
        angular = angular + turn_by(m.inertia[i], dw[i]) + cross(r, dv[i]) * kg;
        inertia = add(inertia, add(m.inertia[i], point_inertia(kg, r)));
    }
    const V3 dv0 = linear * (-1.0f / total), dw0 = -turn_by(inverse(inertia), angular);
    for (std::size_t i = 1; i < part_count; ++i) {
        if (!written[i]) continue;
        dw[i] = dw[i] + dw0;
        dv[i] = dv[i] + dv0 + cross(dw0, m.at[i] - centre);
    }
}

// The arms' reflex delay at Reaction speed 100% (protective arm muscles switch on 100 to 120 ms into a fall), and the
// neck's (a startle, 60 ms).
constexpr float arm_latency = 0.10f, neck_latency = 0.06f;
// The stops: inside 15 degrees of one a joint's speed into it is cut so it comes to rest on it in 80 ms; past it (the
// game's own range is a little wider, and a slam can push a joint there) it goes no further, but it is not pushed back:
// a stop that pushes back is a motor, and one that springs back adds energy.
constexpr float stop_zone = 15 * deg, stop_brake = 0.08f, stop_back = 0.0f;
}

const char* name(Phase phase) noexcept {
    switch (phase) {
    case Phase::idle: return "idle";
    case Phase::brace: return "brace";
    case Phase::impact: return "impact";
    case Phase::tumble: return "tumble";
    case Phase::slide: return "slide";
    case Phase::settle: return "settle";
    case Phase::rest: return "rest";
    case Phase::stumble: return "stumble";
    }
    return "?";
}
const char* name(Fall fall) noexcept {
    switch (fall) {
    case Fall::unknown: return "unknown";
    case Fall::forward: return "forward";
    case Fall::backward: return "backward";
    case Fall::left: return "left";
    case Fall::right: return "right";
    case Fall::drop: return "drop";
    case Fall::spin: return "spin";
    }
    return "?";
}
float torque_limit(Part child, float strength) noexcept {
    return child < part_count ? body::shapes[child].torque * fin(strength, 0.25f, 2.0f, 1.0f) : 0.0f;
}

void Muscles::enter(Phase next) noexcept {
    if (phase_ != next) in_phase_ = 0;
    phase_ = next;
}

void Muscles::begin(const Input& in, std::uint32_t seed) noexcept {
    *this = Muscles{};
    started_ = true;
    phase_ = Phase::brace;
    // A small hash of the seed for each choice, -1..1.
    std::uint32_t h = seed * 2654435761u + 0x9e3779b9u;
    const auto next = [&h] {
        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
        return static_cast<float>(h & 0xffff) / 32767.5f - 1.0f;
    };
    // One arm goes first and the other 20 to 50 ms after it; a hand goes to what hurts 0.7 to 1.3 s after he is still
    // (a person lies stunned for a moment first).
    const float first = next(), later = 0.02f + 0.03f * std::abs(next());
    arm_delay_ = first > 0 ? std::array<float, 2>{0.0f, later} : std::array<float, 2>{later, 0.0f};
    grab_delay_ = 1.0f + 0.3f * next();
    const auto& b = in.body;
    for (auto& r : rest_) r = identity3();
    for (std::size_t i = 1; i < part_count; ++i)
        if (b[i].ok && b[parent_of[i]].ok && finite(b[i].r) && finite(b[parent_of[i]].r)) rest_[i] = multiply(transposed(b[parent_of[i]].r), b[i].r);
    for (auto& m : base_) m = identity3();
    // The tone he rode with: back and legs a little tense, arms looser.
    for (std::size_t k = 0; k < joint_count; ++k) {
        const Group g = group_of(ranges()[k].child);
        act_[k] = g == arm_left || g == arm_right ? 0.08f : 0.15f;
    }
    // The ground: under the feet when they touch it now (a bail off the board on the ground).
    for (const Part p : {l_foot, r_foot, l_toe, r_toe})
        if (b[p].ok && b[p].touching) {
            const float y = b[p].has_point ? b[p].point[1] : b[p].p[1] - 0.06f;
            ground_ = ground_known_ ? std::min(ground_, y) : y;
            ground_known_ = true;
        }
}

Output Muscles::step(const Input& in) noexcept {
    Output out;
    out.phase = phase_;
    const auto& b = in.body;
    for (std::size_t i = 1; i < part_count; ++i)
        if (!b[i].ok || !finite(b[i].w) || !finite(b[i].v) || !finite(b[i].p) || !finite(b[i].r)) return out;
    if (!started_ || phase_ == Phase::idle || !(in.dt > 0) || in.dt > 0.1f) return out;
    const float dt = in.dt;
    t_ += dt;
    in_phase_ += dt;
    const float t = static_cast<float>(t_);
    const float strength = fin(in.strength, 0.25f, 2.0f, 1.0f);
    const float reaction = fin(in.tune.reaction, 0.5f, 2.0f, 1.0f);
    const float head_k = fin(in.tune.head, 0.0f, 1.5f, 1.0f);
    const float tuck_k = fin(in.tune.tuck, 0.0f, 1.5f, 1.0f);
    const float lie_still = fin(in.tune.lie_still, 0.5f, 4.0f, 1.6f);
    const float hold_hurt = fin(in.tune.hold_hurt, 1.0f, 10.0f, 4.5f);

    const body::Mass mass = body::measure(b);
    if (!finite(mass.com) || !finite(mass.velocity) || !finite(mass.spin) || !(mass.total > 0)) return out;
    body::Tree tree;
    tree.build(mass, b);
    const auto centre = [&](std::size_t i) { return mass.com + mass.at[i]; };

    // ---- the read ------------------------------------------------------------------------------------------------
    bool any_touch = false, torso_touch = false;
    float torso_hit = 0, upper_hit = 0;
    for (std::size_t i = 1; i < part_count; ++i) {
        if (!b[i].touching) continue;
        const auto p = static_cast<Part>(i);
        const float hit = fin(b[i].impact, 0.0f, 50.0f, 0.0f);
        any_touch = true;
        if (is_torso(p)) {
            torso_touch = true;
            torso_hit = std::max(torso_hit, hit);
        }
        // A hand, forearm, foot or shin landing is a catch or his feet, not the bail's impact.
        if (!read::aims_down(p) && p != l_shin && p != r_shin) upper_hit = std::max(upper_hit, hit);
    }
    untouched_for_ = any_touch ? 0 : untouched_for_ + dt;
    torso_down_for_ = torso_touch ? torso_down_for_ + dt : 0;
    torso_off_for_ = torso_touch ? 0 : torso_off_for_ + dt;
    // The ground: the lowest floor he touches now (a kerb or a ledge counts until he goes below it). Below everything he
    // has touched and touching nothing (off a ledge, a rail, a set of stairs), it is somewhere under him: taken as most
    // of a body's height below his lowest point, so the arms are ready.
    float lowest = 1e9f, contact_low = 1e9f;
    for (std::size_t i = 1; i < part_count; ++i) {
        const float surface = centre(i)[1] - body::shapes[i].radius;
        lowest = std::min(lowest, surface);
        if (!b[i].touching) continue;
        if (b[i].has_normal && finite(b[i].normal) && std::abs(b[i].normal[1]) < 0.5f) continue; // a wall
        contact_low = std::min(contact_low, b[i].has_point && finite(b[i].point) ? b[i].point[1] : surface);
    }
    if (contact_low < 1e8f) {
        ground_ = contact_low;
        ground_known_ = true;
    } else if (ground_known_ && lowest < ground_ - 0.1f) {
        ground_known_ = false;
    }
    const float ground = ground_known_ ? ground_ : lowest - 0.8f;
    // Standing or kneeling on something under his centre: he goes over it.
    bool support = false;
    V3 pivot{};
    {
        int n = 0;
        for (const Part p : {l_foot, r_foot, l_toe, r_toe, l_shin, r_shin, l_thigh, r_thigh}) {
            if (!b[p].touching) continue;
            const V3 at = b[p].has_point && finite(b[p].point) ? b[p].point : centre(p) - world_up * body::shapes[p].radius;
            if (at[1] > mass.com[1] - 0.35f) continue;
            pivot = pivot + at;
            ++n;
        }
        if (n > 0 && !torso_touch) {
            support = true;
            pivot = pivot * (1.0f / static_cast<float>(n));
        }
    }
    std::array<bool, part_count> skip{};
    for (std::size_t i = 1; i < part_count; ++i) skip[i] = b[i].touching;
    const read::Prediction pred = read::predict(mass, skip, ground, support, pivot);
    const float to_hit = pred.hit ? pred.t : read::look_ahead;
    out.time_to_hit = to_hit;
    out.hit_speed = pred.hit ? std::max(0.0f, -pred.velocity[1]) : 0.0f;
    out.hits_first = pred.hit ? pred.first : board;

    // The torso's frame (x up the spine, y forward, z to his left), and which way he faces over the ground: across his
    // side axis, which stays level whether he pitches forwards or backwards.
    const M3& chest = b[chest3].r;
    const V3 spine_up = normalized(b[neck].p - b[pelvis].p, chest[0]);
    const float tilt = std::acos(std::clamp(dot(spine_up, world_up), -1.0f, 1.0f));
    const V3 facing = normalized(flat(cross(chest[2], world_up)) + flat(chest[1]) * 0.3f, normalized(flat(chest[1]), V3{0, 0, 1}));
    const V3 to_left = cross(world_up, facing);
    const float flat_speed = length(flat(mass.velocity));
    const float torso_spin = 0.5f * (length(b[pelvis].w) + length(b[chest3].w));
    const bool going_down = phase_ == Phase::brace;
    const bool airborne = untouched_for_ > 0.2;

    // Which way he goes over: how his upper chest turns about his centre by the time he lands (not where he travels: off
    // a ledge at speed he still goes over backwards if he turns backwards), and his lean. Not known until he leans, turns
    // or is about to land; then it turns no faster than 4 rad/s, so the arms do not swap sides on a wobble.
    if (going_down) {
        V3 falling = flat(turn_by(pred.turn, mass.at[chest3]) - mass.at[chest3]) + flat(spine_up) * 0.3f;
        const float size = length(falling);
        if (size > 0.15f) {
            falling = falling * (1.0f / size);
            if (!fall_known_) {
                if (tilt > 12 * deg || flat_speed > 1.0f || to_hit < 0.6f) {
                    fall_ = falling;
                    fall_known_ = true;
                }
            } else {
                fall_ = normalized(flat(turn_towards(fall_, falling, 4.0f * dt)), fall_);
            }
        }
    }
    // The kind of fall, with a little hysteresis: a new kind has to hold for 80 ms.
    if (going_down) {
        Fall want = Fall::unknown;
        if (length(mass.spin) > 6.0f && untouched_for_ > 0.1) want = Fall::spin;
        else if (fall_known_) {
            const float ahead = dot(fall_, facing), left = dot(fall_, to_left);
            want = ahead > 0.55f ? Fall::forward : ahead < -0.55f ? Fall::backward : left > 0 ? Fall::left : Fall::right;
        } else if (mass.velocity[1] < -2.0f && untouched_for_ > 0.1) want = Fall::drop;
        if (want != kind_) {
            if (want == wanted_) wanted_for_ += dt;
            else {
                wanted_ = want;
                wanted_for_ = 0;
            }
            if (kind_ == Fall::unknown || wanted_for_ >= 0.08) kind_ = want;
        } else {
            wanted_ = want;
            wanted_for_ = 0;
        }
    }
    out.fall = kind_;

    // What hit hardest, for a hand to go to once he lies still: not the hands and feet (they land on purpose); the head
    // (a chin or a face) counts most, a knee for a shin or a thigh.
    for (std::size_t i = 1; i < part_count; ++i) {
        auto p = static_cast<Part>(i);
        if (!b[i].touching || is_end(p)) continue;
        if (p == neck) p = head;
        if (p == l_thigh) p = l_shin;
        if (p == r_thigh) p = r_shin;
        const float score = fin(b[i].impact, 0.0f, 50.0f, 0.0f) * (p == head ? 1.6f : p == l_shin || p == r_shin ? 1.2f : 1.0f);
        if (score > hurt_impact_ + 0.5f) {
            hurt_impact_ = score;
            hurt_ = p;
        }
    }
    // An arm that takes a hit itself goes loose; a hand coming down is the catch.
    for (int s = 0; s < 2; ++s) {
        const auto i = static_cast<std::size_t>(s);
        float hit = 0;
        for (const Part p : s == 0 ? std::array<Part, 3>{l_arm, l_forearm, l_collar} : std::array<Part, 3>{r_arm, r_forearm, r_collar})
            if (b[p].touching) hit = std::max(hit, fin(b[p].impact, 0.0f, 50.0f, 0.0f));
        if (hit > 2.5f) arm_hit_at_[i] = t_;
        const Part hand = s ? r_hand : l_hand, fore = s ? r_forearm : l_forearm;
        if (going_down && caught_at_[i] < 0 && (b[hand].touching || b[fore].touching)) caught_at_[i] = t_;
    }

    // ---- the phase -----------------------------------------------------------------------------------------------
    // Hit: stunned for a moment, as long and as deep as the hit (0.15 s and a little for a soft landing, up to 0.6 s and
    // most of the tone for a slam), so the body goes wherever the momentum sends it.
    const auto stun = [&](float speed) {
        stun_until_ = t_ + std::clamp(0.15f + 0.1f * (speed - 1.5f), 0.15f, 0.6f);
        stun_depth_ = std::clamp(0.3f + 0.12f * (speed - 1.5f), 0.3f, 0.9f);
        last_stun_ = t_;
    };
    // Lying still: his torso down, and the whole of him nearly at rest (under about 0.45 m/s on average, hardly turning);
    // moving again is well over that (the gap keeps him from flickering between the two).
    const float energy = mass.energy / mass.total; // J per kg: half the mean square speed
    const bool on_ground = torso_off_for_ < 0.25 && !airborne;
    still_ = still_ ? on_ground && energy < 0.4f && torso_spin < 2.0f : on_ground && energy < 0.1f && torso_spin < 1.0f;
    still_for_ = still_ ? still_for_ + dt : 0;
    const bool falling_again = airborne && mass.velocity[1] < -1.5f;
    const bool hit = (torso_touch && torso_hit > 1.2f) || upper_hit > 3.0f || torso_down_for_ > 0.06;
    const bool hurt = in.tune.grab_hurt && hurt_ != board && hurt_impact_ > 2.6f;
    const auto moving = [&] { return torso_spin > 4.5f ? Phase::tumble : Phase::slide; };
    switch (phase_) {
    case Phase::brace:
    case Phase::stumble:
        if (hit) {
            stun(std::max(torso_hit, upper_hit));
            enter(Phase::impact);
        }
        break;
    case Phase::impact:
        if (t_ >= stun_until_ && in_phase_ > 0.12) enter(falling_again ? Phase::brace : moving());
        break;
    case Phase::tumble:
        if (falling_again) enter(Phase::brace);
        else if (still_for_ > 0.3) enter(Phase::settle);
        else if (torso_spin < 2.5f && in_phase_ > 0.2) enter(Phase::slide);
        break;
    case Phase::slide:
        if (falling_again) enter(Phase::brace);
        else if (still_for_ > 0.3) enter(Phase::settle);
        else if (torso_spin > 5.5f) enter(Phase::tumble);
        break;
    case Phase::settle:
        if (falling_again) enter(Phase::brace);
        else if (!still_) enter(moving()); // moving again: whatever he was doing lying there stops at once
        else if (in_phase_ > lie_still && (!hurt || held_ >= hold_hurt)) enter(Phase::rest);
        break;
    case Phase::rest:
        if (falling_again) enter(Phase::brace);
        else if (!still_) enter(moving());
        break;
    case Phase::idle: return out;
    }
    // A new hard hit once down (rolling into a wall, a second slam down the stairs) stuns him again.
    if (phase_ != Phase::brace && phase_ != Phase::impact && torso_hit > 3.0f && t_ - last_stun_ > 0.35) {
        stun(torso_hit);
        enter(Phase::impact);
    }
    if (phase_ == Phase::brace && in_phase_ < dt * 1.5) caught_at_ = {-1, -1}; // falling again: a new catch
    out.phase = phase_;

    // The pain reaction: only lying still, a while after he stopped, for as long as Hold what hurts for says; never while
    // he moves (moving again ends it, and it starts over once he is still again).
    const bool pain = phase_ == Phase::settle && hurt && still_for_ >= grab_delay_ && held_ < hold_hurt;
    if (pain && !pain_) {
        // Which hand goes to it: the other side's, unless that one is out of reach; both for the head or a knee when
        // both reach. What he curls from is how he lies now.
        const V3 at = b[hurt_].p;
        const float left = length(at - b[l_arm].p), right = length(at - b[r_arm].p);
        const bool on_left = hurt_ == l_forearm || hurt_ == l_arm || hurt_ == l_collar;
        const bool on_right = hurt_ == r_forearm || hurt_ == r_arm || hurt_ == r_collar;
        hurt_hand_ = on_left ? 1 : on_right ? -1 : left < right ? -1 : 1;
        if ((hurt_ == head || hurt_ == l_shin || hurt_ == r_shin) && std::max(left, right) < 0.7f) hurt_hand_ = 2;
        if (std::min(left, right) > 0.7f && !on_left && !on_right) hurt_hand_ = 0;
        for (std::size_t k = 0; k < joint_count; ++k) {
            const auto& l = ranges()[k];
            base_[k] = multiply(transposed(b[l.frame].r), b[l.child].r);
        }
    }
    if (phase_ != Phase::settle && phase_ != Phase::rest) held_ = 0;
    pain_ = pain;
    if (pain) held_ += dt;
    out.pain = pain;

    // ---- what each joint is asked ----------------------------------------------------------------------------------
    // Tone, by group, for the phase. The neck's is Head protection's.
    const float stunned = t_ < stun_until_ ? stun_depth_ : 0.0f;
    std::array<float, 6> tone{};
    switch (phase_) {
    case Phase::brace:
    case Phase::stumble: tone = {0.2f, 0.2f, 0.1f, 0.1f, 0.15f, 0.15f}; break;
    case Phase::impact: {
        const float limp = 1 - stunned;
        tone = {0.2f * limp, 0.2f * (1 - 0.5f * stunned), 0.08f * limp, 0.08f * limp, 0.15f * limp, 0.15f * limp};
        break;
    }
    case Phase::tumble:
    case Phase::slide: tone = {0.12f, 0.25f, 0.08f, 0.08f, 0.08f, 0.08f}; break;
    case Phase::settle: {
        const float f = std::clamp(static_cast<float>(in_phase_) / lie_still, 0.0f, 1.0f);
        tone = {lerp(0.08f, 0.03f, f), lerp(0.15f, 0.04f, f), lerp(0.06f, 0.03f, f), lerp(0.06f, 0.03f, f), lerp(0.05f, 0.025f, f),
                lerp(0.05f, 0.025f, f)};
        break;
    }
    case Phase::rest: tone = {0.025f, 0.03f, 0.025f, 0.025f, 0.02f, 0.02f}; break;
    case Phase::idle: break;
    }
    tone[neck_group] *= head_k;
    std::array<Drive, joint_count> drive{};
    for (std::size_t k = 0; k < joint_count; ++k) {
        auto& d = drive[k];
        d.u = tone[group_of(ranges()[k].child)];
        // Tense muscle resists being turned fast: from about 1/s (lying there) to 3.5/s (falling, tense).
        d.damp = 1.0f + 12.0f * d.u * strength;
    }
    const auto drive_of = [&](Part c) -> Drive& { return drive[index_of()[c]]; };
    const auto rel_of = [&](Part c) {
        const auto& l = ranges()[index_of()[c]];
        return multiply(transposed(b[l.frame].r), b[c].r);
    };
    // A ball joint's goal: its bone swung to point along `dir` (the frame's axes), its twist left as it is.
    const auto swing_goal = [&](Part c, const V3& dir) {
        const M3 now = rel_of(c);
        return multiply(rotation(swing_between(normalized(now[0], dir), dir)), now);
    };

    // The neck: tone only, but going down backwards the chin tucks in a little (a light pull, which a slam throws off).
    {
        const bool tuck_chin = (phase_ == Phase::brace || phase_ == Phase::impact) && kind_ == Fall::backward && t * reaction >= neck_latency &&
                               head_k > 0.05f;
        // Eased in; let go at once (letting go only leaves the neck to its tone, so nothing snaps).
        chin_ = tuck_chin ? std::min(chin_ + 2.0f * dt, 0.45f) : 0.0f;
        if (chin_ > 0.0f)
            for (const Part p : {neck, head}) {
                auto& d = drive_of(p);
                d.goal = true;
                d.want = rotation(V3{0, 0, chin_ * (p == neck ? 0.65f : 0.35f)});
                d.wn = 5;
                d.zeta = 1;
            }
    }

    // The arms: the one reflex in the bail.
    for (int s = 0; s < 2; ++s) {
        const auto i = static_cast<std::size_t>(s);
        const Part upper = s ? r_arm : l_arm, fore = s ? r_forearm : l_forearm, hand = s ? r_hand : l_hand;
        const float out_z = s == 0 ? 1.0f : -1.0f; // this arm's outside, as the chest's z
        const float arm_len = length(b[fore].p - b[upper].p) + length(b[hand].p - b[fore].p) + 0.08f;
        out.hand_mark[i] = V3{std::nanf(""), std::nanf(""), std::nanf("")}; // aiming nowhere
        if (phase_ == Phase::brace || phase_ == Phase::impact) {
            if (t_ - arm_hit_at_[i] < 0.6) {
                // Hit itself: loose.
                for (const Part p : {upper, fore, hand}) drive_of(p).u = std::min(drive_of(p).u, 0.05f);
                continue;
            }
            const double since_catch = caught_at_[i] >= 0 ? t_ - caught_at_[i] : -1;
            if (since_catch >= 0) {
                // Down on the ground: for a quarter of a second the arm resists folding as hard as its strength lets it,
                // and folds as far as the slam makes it; then it is as loose as the rest of him.
                for (const Part p : {upper, fore}) {
                    auto& d = drive_of(p);
                    d.u = since_catch < 0.25 ? 0.45f : 0.06f;
                    d.damp = since_catch < 0.25 ? 8.0f : 1.5f;
                }
                continue;
            }
        }
        const float onset = (arm_latency + arm_delay_[i]) / reaction;
        const bool reach = in.tune.catch_arms && going_down && t >= onset && pred.hit &&
                           (kind_ == Fall::forward || kind_ == Fall::backward || kind_ == Fall::left || kind_ == Fall::right || kind_ == Fall::drop);
        if (reach) {
            // Out towards where he will land, as the arm should be when he gets there: the direction is set in the world
            // (down to the ground, ahead of him, to the side he goes, or behind him) and read in the upper chest's axes as
            // they will be by then, so it is where the ground will be for the arm, not where it is now.
            const V3 outward = to_left * out_z;
            V3 world_dir{};
            float firm = 1.0f;
            switch (kind_) {
            case Fall::forward: world_dir = -world_up + fall_ * 0.6f + outward * 0.2f; break;
            case Fall::backward:
                world_dir = -world_up + fall_ * 0.4f + outward * 0.8f; // back, down and out to the side: a slap, not a post
                firm = 0.7f;
                break;
            default: world_dir = -world_up + fall_ * 0.55f + facing * 0.25f; break; // both hands to the side he goes
            }
            // Dropping straight down (no way over yet): both hands come up a little in front of him, ready.
            const V3 dir = kind_ == Fall::drop ? normalized(V3{-0.5f, 0.7f, 0.5f * out_z})
                                               : turn_back(multiply(pred.turn, chest), normalized(world_dir));
            if (kind_ == Fall::drop) firm = 0.6f;
            // Little time before the hit: out quicker, and it arrives bent; with time it is long.
            const float urgency = std::clamp((0.7f - to_hit) / 0.5f, 0.0f, 1.0f);
            const float late = std::clamp((0.35f - to_hit) / 0.25f, 0.0f, 1.0f);
            const V3 now = normalized(rel_of(upper)[0]);
            if (length(aim_[i]) < 0.5f) aim_[i] = now; // the aim starts where the arm is
            aim_[i] = turn_towards(normalized(aim_[i], now), dir, (6.0f + 6.0f * urgency) * reaction * smooth((t - onset) / 0.1f) * dt);
            auto& sh = drive_of(upper);
            sh.goal = true;
            sh.want = swing_goal(upper, aim_[i]);
            sh.wn = std::clamp(3.0f / std::max(to_hit - 0.08f, 0.05f), 6.0f, 13.0f);
            sh.zeta = 0.8f;
            sh.u = (0.3f + 0.35f * urgency) * firm;
            auto& el = drive_of(fore);
            el.goal = true;
            el.want = rotation(V3{0, 0, (25 + 15 * late) * deg});
            el.wn = 10;
            el.zeta = 0.8f;
            el.u = sh.u;
            drive_of(hand).u = std::max(drive_of(hand).u, 0.15f);
            out.hand_mark[i] = b[upper].p + turn_by(chest, aim_[i]) * arm_len;
            continue;
        }
        if (!going_down) aim_[i] = {};
        const bool this_hand = hurt_hand_ == 2 || (hurt_hand_ < 0 && s == 0) || (hurt_hand_ > 0 && s == 1);
        if (pain && this_hand) {
            // A hand to what hurts, slowly and lightly: the arm only, with about what it takes to move an arm.
            V3 spot = centre(hurt_) + world_up * 0.07f;
            if (hurt_ == head) spot = centre(head) + b[head].r[1] * 0.1f + chest[2] * (0.04f * out_z);
            if (hurt_ == l_shin || hurt_ == r_shin) spot = b[hurt_].p + world_up * 0.06f + chest[2] * (0.05f * out_z);
            if (hurt_ == l_forearm || hurt_ == l_arm) spot = b[l_forearm].p + world_up * 0.05f;
            if (hurt_ == r_forearm || hurt_ == r_arm) spot = b[r_forearm].p + world_up * 0.05f;
            const V3 shoulder = b[upper].p;
            const float a = length(b[fore].p - shoulder), c = length(b[hand].p - b[fore].p) + 0.07f;
            const V3 toward = normalized(spot - shoulder, -chest[0]);
            V3 pole = cross(toward, b[upper].r[2]);
            if (length(pole) < 0.15f) pole = -chest[1];
            const V3 elbow_at = reach_middle(shoulder, spot, a, c, pole);
            const float d = std::clamp(length(spot - shoulder), std::abs(a - c) + 0.02f, (a + c) * 0.98f);
            const float bend = pi - std::acos(std::clamp((a * a + c * c - d * d) / (2 * a * c), -1.0f, 1.0f));
            auto& sh = drive_of(upper);
            sh.goal = true;
            sh.want = swing_goal(upper, turn_back(chest, normalized(elbow_at - shoulder, toward)));
            sh.wn = 3;
            sh.zeta = 1;
            sh.u = 0.1f;
            auto& el = drive_of(fore);
            el.goal = true;
            el.want = rotation(V3{0, 0, bend});
            el.wn = 3;
            el.zeta = 1;
            el.u = 0.1f;
            out.hand_mark[i] = spot;
        }
    }
    // Hurt and lying still: he curls a little, as far as Tuck says: the back rounded, the knees drawn up while his feet
    // stay on the ground (a leg whose foot is off the ground is left as it lies). Lightly, from how he lay.
    if (pain && tuck_k > 0.01f) {
        for (const Part p : {spine, back1, chest2, chest3}) {
            auto& d = drive_of(p);
            d.goal = true;
            d.want = multiply(rotation(V3{0, 0, 0.08f * tuck_k}), base_[index_of()[p]]);
            d.wn = 3;
            d.zeta = 1;
            d.u = 0.05f;
        }
        for (int s = 0; s < 2; ++s) {
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin, foot = s ? r_foot : l_foot, toe = s ? r_toe : l_toe;
            if (!b[foot].touching && !b[toe].touching) continue;
            auto& hip = drive_of(thigh);
            hip.goal = true;
            hip.want = multiply(rotation(V3{0, 0, -0.25f * tuck_k}), base_[index_of()[thigh]]); // -z swings a thigh forwards
            hip.wn = 3;
            hip.zeta = 1;
            hip.u = 0.05f;
            auto& knee = drive_of(shin);
            knee.goal = true;
            knee.want = rotation(V3{0, 0, std::min(bend_of(base_[index_of()[shin]]) + 0.5f * tuck_k, 100 * deg)});
            knee.wn = 3;
            knee.zeta = 1;
            knee.u = 0.05f;
        }
    }
    for (int s = 0; s < 2; ++s) out.foot_mark[static_cast<std::size_t>(s)] = b[s ? r_foot : l_foot].p;

    // ---- the muscles ---------------------------------------------------------------------------------------------
    // Each joint's activation follows what it is asked (40 ms to tense, 100 ms to let go). Tone is a damper on the
    // joint's own turning; a goal is a spring and damper towards it. Both are set against the joint's inertia as the free
    // linked bodies present it, so a damping rate or a natural frequency means what it says whatever hangs off the joint,
    // and the whole body's are solved together in one step as a stable PD (Tan, Liu and Turk 2011: taken at the end of
    // the step, so no stiffness makes it ring), the linked bodies moving as Featherstone's method has them
    // (real_bails_body.h). A joint whose torque comes out over what it has (activation x its strength) is held to that
    // and the rest solved again.
    const std::array<M3, part_count> apparent = tree.respond();
    // A hand, foot or toes on the ground is the ground's: it is not written (forced, a light end body flicks round), and
    // its own joint is left to the ground and the game (a torque there would only push the forearm or shin against an
    // end that does not move with it).
    std::array<bool, part_count> planted{}, written_body{};
    for (std::size_t i = 1; i < part_count; ++i) {
        planted[i] = is_end(static_cast<Part>(i)) && b[i].touching;
        written_body[i] = !planted[i];
    }
    std::array<V3, part_count> push{}, held{};
    std::array<M3, part_count> armature{};
    std::array<float, part_count> cap{};
    std::array<bool, part_count> saturated{};
    for (std::size_t k = 0; k < joint_count; ++k) {
        const auto& l = ranges()[k];
        const Part c = l.child;
        const Drive& d = drive[k];
        const float u = std::clamp(d.u, 0.0f, 1.0f);
        act_[k] += (u - act_[k]) * (1.0f - std::exp(-dt / (u > act_[k] ? 0.04f : 0.1f)));
        out.activation[c] = act_[k];
        out.driven[c] = d.goal;
        cap[c] = std::max(act_[k], 0.02f) * body::shapes[c].torque * strength;
        if (body::weld(c) || planted[c]) continue;
        const M3& frame = b[l.frame].r;
        const M3& child = b[c].r;
        const V3 rate = b[c].w - b[l.frame].w;
        float kp = 0, kd = d.damp;
        V3 e{};
        if (d.goal) {
            kp = d.wn * d.wn * strength;
            kd = 2 * d.zeta * d.wn * std::sqrt(strength) + 1.0f; // and a little passive viscosity
            const M3 want = multiply(transposed(frame), limits::clamp_target(l, frame, multiply(frame, d.want), rest_));
            if (body::hinge(c)) {
                // Kept 8 degrees inside its stops (an elbow 0 to 145, a knee 10 to 150).
                const float goal = std::clamp(bend_of(want), l.r[2].lo + 8 * deg, l.r[2].hi - 8 * deg);
                e = tree.axis[c] * (goal - bend_of(multiply(transposed(frame), child)));
            } else {
                e = turn_to(child, multiply(frame, want));
            }
        }
        armature[c] = scaled(apparent[c], kd * dt + kp * dt * dt);
        if (body::hinge(c)) {
            // An elbow or knee bends about its upper bone's z only (the game holds the other two axes).
            const V3 n = tree.axis[c];
            const float q_rate = dot(rate, n), q_e = dot(e, n);
            push[c] = n * (apparent[c][0][0] * (kp * (q_e - dt * q_rate) - kd * q_rate) * dt);
        } else {
            push[c] = turn_by(apparent[c], (e - rate * dt) * kp - rate * kd) * dt;
        }
        if (!finite(push[c]) || !finite(armature[c])) {
            push[c] = {};
            armature[c] = {};
        }
    }
    float mean = 0;
    for (const float a : act_) mean += a;
    out.tone = mean / static_cast<float>(joint_count);
    // The torque a joint ends up with, from the solve: what it pushed less what its own spring and damper took back as
    // it moved.
    std::array<V3, part_count> dw{}, dv{};
    const auto torque_of = [&](Part c) {
        if (saturated[c]) return held[c] * (1.0f / dt);
        if (body::weld(c)) return V3{};
        const V3 turn = dw[c] - dw[parent_of[c]];
        if (body::hinge(c)) return tree.axis[c] * ((dot(push[c], tree.axis[c]) - armature[c][0][0] * dot(turn, tree.axis[c])) / dt);
        return (push[c] - turn_by(armature[c], turn)) * (1.0f / dt);
    };
    for (int round = 0; round < 5; ++round) {
        std::array<M3, part_count> joint_armature{};
        std::array<V3, part_count> impulse{};
        for (std::size_t k = 1; k < body::chain.size(); ++k) {
            const Part c = body::chain[k];
            joint_armature[c] = saturated[c] ? M3{} : armature[c];
            impulse[c] = saturated[c] ? held[c] : push[c];
        }
        tree.articulate(joint_armature);
        tree.solve(impulse, dw, dv);
        bool changed = false;
        for (std::size_t k = 1; k < body::chain.size() && round < 4; ++k) {
            const Part c = body::chain[k];
            if (saturated[c]) continue;
            const V3 torque = torque_of(c);
            if (length(torque) > cap[c] * 1.0001f) {
                saturated[c] = true;
                held[c] = clamp_length(torque, cap[c]) * dt;
                changed = true;
            }
        }
        if (!changed) break;
    }
    for (std::size_t k = 1; k < body::chain.size(); ++k) {
        const Part c = body::chain[k];
        out.torque[c] = length(torque_of(c));
    }
    out.work = body::energy_of(mass, b, dw, dv, written_body);

    // ---- the joints' stops -----------------------------------------------------------------------------------------
    // Every joint, every step: its speed into a stop (with what the muscles add) is cut so it comes to rest on it, and
    // past it it goes no further. Stops are bone and ligament, not muscle: no strength caps them. Each is an impulse at
    // its joint through the linked bodies (the tone of the other joints resisting it, so a knee that hits its stop jerks
    // the thigh, not the whole of him), one joint after the other from the pelvis out, twice over, as a physics engine's
    // solver goes.
    std::array<M3, part_count> stiff{};
    bool stiff_known = false;
    for (int round = 0; round < 2; ++round) {
        bool any = false;
        for (const auto& l : ranges()) {
            const Part c = l.child;
            if (body::weld(c) || planted[c]) continue; // the game's own joint keeps a collarbone, the ground a planted end
            const V3 now = (b[c].w + dw[c]) - (b[l.frame].w + dw[l.frame]);
            const V3 fix = clamp_length(limits::stop_fix(l, limits::measure(l, b, rest_), now, stop_zone, stop_brake, stop_back), 20.0f);
            if (dot(fix, fix) < 1e-6f) continue;
            if (!stiff_known) {
                stiff = tree.respond();
                stiff_known = true;
            }
            std::array<V3, part_count> stop{}, sw{}, sv{};
            stop[c] = body::hinge(c) ? tree.axis[c] * (stiff[c][0][0] * dot(fix, tree.axis[c])) : turn_by(stiff[c], fix);
            if (!finite(stop[c])) continue;
            tree.solve(stop, sw, sv);
            for (std::size_t i = 1; i < part_count; ++i) {
                dw[i] = dw[i] + sw[i];
                dv[i] = dv[i] + sv[i];
            }
            any = true;
        }
        if (!any) break;
    }

    // ---- the bodies --------------------------------------------------------------------------------------------------
    keep_momentum(mass, written_body, dw, dv);
    // Only what the muscles and stops add is bounded (at most 25 rad/s and 40 m/s, or the body's own if it is going
    // faster already): the game's own motion is never cut.
    Output written = out;
    for (std::size_t i = 1; i < part_count; ++i) {
        if (!written_body[i]) continue;
        const V3 w = clamp_length(b[i].w + dw[i], std::max(25.0f, length(b[i].w)));
        const V3 v = clamp_length(b[i].v + dv[i], std::max(40.0f, length(b[i].v)));
        if (!finite(w) || !finite(v)) return out; // nothing written rather than something broken
        if (dot(dw[i], dw[i]) < 1e-8f && dot(dv[i], dv[i]) < 1e-8f) continue;
        written.w[i] = w;
        written.v[i] = v;
        written.write[i] = true;
    }
    return written;
}
}
