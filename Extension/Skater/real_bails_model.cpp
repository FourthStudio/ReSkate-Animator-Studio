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
bool is_foot(Part p) noexcept { return p == l_foot || p == r_foot || p == l_toe || p == r_toe; }
bool is_torso(Part p) noexcept {
    return p == pelvis || p == spine || p == back1 || p == chest2 || p == chest3 || p == neck || p == head || p == l_collar || p == r_collar;
}
// Each body's own bone runs from its joint to its child's (the next one out), for where on it a hit was.
Part child_of(Part p) noexcept {
    switch (p) {
    case neck: return head;
    case l_forearm: return l_hand;
    case l_arm: return l_forearm;
    case l_collar: return l_arm;
    case r_forearm: return r_hand;
    case r_arm: return r_forearm;
    case r_collar: return r_arm;
    case chest3: return neck;
    case chest2: return chest3;
    case back1: return chest2;
    case spine: return back1;
    case l_foot: return l_toe;
    case l_shin: return l_foot;
    case l_thigh: return l_shin;
    case r_foot: return r_toe;
    case r_shin: return r_foot;
    case r_thigh: return r_shin;
    case pelvis: return spine;
    default: return board; // the head, the hands and the toes end their chains
    }
}

// The share of a push on a joint the game keeps, by how fast that joint turns now (rad/s, child against parent). Fitted
// to the user's 70 recorded bails of 2026-11-02 (each step: what came of a written change of a joint's turning, with the
// joint's own carry-over taken out): under about 0.6 rad/s 1-4% of any push however large (the game holds a still joint
// where it is), 8-13% at 1-2.5 rad/s, 15-30% at 2.5-4 and 30-45% over that; the same in the air and on the ground.
// Elbows keep less, knees more.
float share_kept(Part c, float turning) noexcept {
    float k = turning < 0.3f ? 0.015f : std::clamp(0.02f + 0.06f * turning, 0.02f, 0.4f);
    if (c == l_forearm || c == r_forearm) k *= 0.7f;
    if (c == l_shin || c == r_shin) k *= 1.3f;
    return k;
}
// What the muscles hand the game for a joint is scaled by one over that share, at most 8 times, so what stays is what
// they mean; and a joint's turning is never changed by more than 1 rad/s in a step when it turns slowly, up to 6 rad/s
// when it turns fast. A still joint keeps next to nothing of any push, so pushing it harder only flicks it the moment the
// game lets go; a joint that gets going is handed over less at once, by how fast it goes then (a running average of what
// the game kept lagged behind: handed over 10 times what was meant just as the game began keeping 40%, the arm flicked).
constexpr float most_gain = 8.0f;
float gain_for(Part c, float turning) noexcept { return std::clamp(1.0f / share_kept(c, turning), 1.0f, most_gain); }
float most_change(float turning) noexcept { return std::min(1.0f + 1.5f * turning, 6.0f); }

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

// How a joint is moved to a goal: as a person's quick move goes, at up to `top` rad/s, slowing only as much as it must to
// stop on it (`brake` rad/s^2), but never under `least` until it is within `near` of it. Under about 0.6 rad/s the game
// holds a joint where it is, so a move that eased in slowly the way a spring does stopped short (in the user's game the
// hands hung in the air a hand's width off what they reached for); this one keeps going at a pace the game lets through
// and stops on the goal, where the game's hold then keeps it.
struct Pace {
    float top{6}, brake{40}, least{1.0f}, near{3 * deg};
};
// The turning (world) that takes axes `now` to `want`.
V3 seek(const M3& now, const M3& want, const Pace& pace) noexcept {
    const V3 e = turn_to(now, want);
    const float a = length(e);
    if (!(a > pace.near)) return {};
    const float speed = std::clamp(std::sqrt(2 * pace.brake * (a - pace.near)), std::min(pace.least, pace.top), pace.top);
    return e * (speed / a);
}
// The same for a hinge's bend (rad/s along its axis).
float seek_bend(float now, float want, const Pace& pace) noexcept {
    const float e = want - now, a = std::abs(e);
    if (!(a > pace.near)) return 0.0f;
    const float speed = std::clamp(std::sqrt(2 * pace.brake * (a - pace.near)), std::min(pace.least, pace.top), pace.top);
    return e > 0 ? speed : -speed;
}

// What a joint's muscles do in one step: push its own turning (child against frame) towards `target`, closing `share` of
// the gap in the step. Tone is a target of no turning at a small share (a damper); a reflex a short pulse; a move its way
// to a goal (seek). Its torque is at most `u` (activation) x its strength.
struct Drive {
    float u{};
    float share{};
    V3 target{};
    bool goal{};
    V3 no_push{};  // a way (world, unit) the joint must not push: a leg whose foot is on the ground does not straighten
                   // against it (that would push him along the ground: a crawl)
    float carry{}; // how much of the weight hanging off the joint its muscles hold up, 0..1 (a hand going to what hurts:
                   // lifted, not dragged; never while falling, where it would make the arms float)
    float accel{}; // how quickly a move may speed the joint up or turn it round, rad/s^2 (0: a reflex's, by its group)
};

// What is written changes the body's momentum by exactly `linear` and `angular` (about com): nothing, but for the board's
// push back on a kicking foot. A hand or foot planted on the ground is not written, so its share of the turns is taken
// back out of the rest as one rigid motion (linear, and angular about their centre), which leaves every joint's own turn
// as it was.
void keep_momentum(const body::Mass& m, const std::array<bool, part_count>& written, std::array<V3, part_count>& dw,
                   std::array<V3, part_count>& dv, const V3& want_linear, const V3& want_angular) noexcept {
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
    const V3 dv0 = (want_linear - linear) * (1.0f / total);
    const V3 dw0 = turn_by(inverse(inertia), want_angular - cross(centre, want_linear) - angular);
    for (std::size_t i = 1; i < part_count; ++i) {
        if (!written[i]) continue;
        dw[i] = dw[i] + dw0;
        dv[i] = dv[i] + dv0 + cross(dw0, m.at[i] - centre);
    }
}

// The arms' reflex delay at Reaction speed 100% (protective arm muscles switch on 100 to 126 ms into a fall), and the
// neck's (the chin starts in 60 to 100 ms).
constexpr float arm_latency = 0.10f, neck_latency = 0.07f;
// The stops: inside 15 degrees of one a joint's speed into it is cut so it comes to rest on it in 80 ms; past it it goes
// no further, but it is not pushed back (a stop that pushes back is a motor, and one that springs back adds energy).
constexpr float stop_zone = 15 * deg, stop_brake = 0.08f, stop_back = 0.0f;
// A complete skateboard: 2 to 3.4 kg, most 2.3 to 2.8.
constexpr float board_mass = 2.5f;
// The kick: 100 to 200 ms, the board leaving him at 2 to 4 m/s.
constexpr float kick_time = 0.16f, kick_speed = 3.0f;
// A reflex's strength `since` seconds after what set it off: nothing for `onset`, up over `rise`, held for `hold`, back
// down over `fall`, smoothly. 0..1.
float burst(double since, float onset, float rise, float hold, float fall) noexcept {
    float x = static_cast<float>(since) - onset;
    if (x < 0) return 0;
    if (x < rise) return smooth(x / rise);
    x -= rise;
    if (x < hold) return 1;
    x -= hold;
    return x < fall ? 1 - smooth(x / fall) : 0.0f;
}

// What hurts by the part that was hit and where on it (`local`: from the body's centre, in its axes, when `known`). The
// trunk's back is the back; its sides the ribs (chest) or a hip (pelvis, spine); its front the ribs or the belly.
Hurt hurt_of(Part p, const V3& local, bool known, bool left_default) noexcept {
    switch (p) {
    case head: case neck: return Hurt::head;
    case l_arm: case l_forearm: case l_collar: return Hurt::left_arm;
    case r_arm: case r_forearm: case r_collar: return Hurt::right_arm;
    case l_hand: return Hurt::left_hand;
    case r_hand: return Hurt::right_hand;
    case l_thigh: case l_shin: return Hurt::left_leg;
    case r_thigh: case r_shin: return Hurt::right_leg;
    default: break;
    }
    const bool lower = p == pelvis || p == spine;
    if (!known) return lower ? Hurt::back : left_default ? Hurt::left_ribs : Hurt::right_ribs;
    const float front = local[1], side = local[2];
    const bool left = std::abs(side) > 0.01f ? side > 0 : left_default;
    if (front < 0 && -front > std::abs(side)) return Hurt::back;
    if (lower && std::abs(side) >= front) return left ? Hurt::left_hip : Hurt::right_hip;
    return left ? Hurt::left_ribs : Hurt::right_ribs;
}
// How much a hit there hurts against the others (head 1, a hand 0.9, an elbow or the ribs 0.8, a knee 0.7, a hip or the
// back 0.6; racked 1.5, the most).
float site_factor(Hurt h) noexcept {
    switch (h) {
    case Hurt::head: return 1.0f;
    case Hurt::left_hand: case Hurt::right_hand: return 0.9f;
    case Hurt::left_arm: case Hurt::right_arm: case Hurt::left_ribs: case Hurt::right_ribs: return 0.8f;
    case Hurt::left_leg: case Hurt::right_leg: return 0.7f;
    case Hurt::left_hip: case Hurt::right_hip: case Hurt::back: return 0.6f;
    case Hurt::groin: return 1.5f;
    case Hurt::none: break;
    }
    return 0;
}
// About how long each is held, s, at Hold what hurts for 4.5 s and a middling hit (it scales with both).
float hold_base(Hurt h) noexcept {
    switch (h) {
    case Hurt::head: return 5;
    case Hurt::left_hand: case Hurt::right_hand: case Hurt::left_ribs: case Hurt::right_ribs: case Hurt::left_leg: case Hurt::right_leg: return 4;
    case Hurt::groin: return 8;
    default: return 3;
    }
}
// Which side it is on: +1 his left, -1 his right, 0 neither.
int side_of(Hurt h) noexcept {
    switch (h) {
    case Hurt::left_arm: case Hurt::left_hand: case Hurt::left_leg: case Hurt::left_hip: case Hurt::left_ribs: return 1;
    case Hurt::right_arm: case Hurt::right_hand: case Hurt::right_leg: case Hurt::right_hip: case Hurt::right_ribs: return -1;
    default: return 0;
    }
}
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
const char* name(Hurt hurt) noexcept {
    switch (hurt) {
    case Hurt::none: return "none";
    case Hurt::head: return "head";
    case Hurt::left_arm: return "left arm";
    case Hurt::right_arm: return "right arm";
    case Hurt::left_hand: return "left hand";
    case Hurt::right_hand: return "right hand";
    case Hurt::left_leg: return "left leg";
    case Hurt::right_leg: return "right leg";
    case Hurt::left_hip: return "left hip";
    case Hurt::right_hip: return "right hip";
    case Hurt::back: return "back";
    case Hurt::left_ribs: return "left ribs";
    case Hurt::right_ribs: return "right ribs";
    case Hurt::groin: return "groin";
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
float Muscles::draw() noexcept {
    // A small hash of the seed and how many were drawn: -1..1.
    std::uint32_t h = (seed_ + 0x9e3779b9u * ++draws_) * 2654435761u;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return static_cast<float>(h & 0xffff) / 32767.5f - 1.0f;
}

void Muscles::begin(const Input& in, std::uint32_t seed) noexcept {
    *this = Muscles{};
    started_ = true;
    phase_ = Phase::brace;
    seed_ = seed * 2654435761u + 0x9e3779b9u;
    // The lead arm goes 20 to 60 ms before the other (which one is chosen from the fall; this is for a fall with no side
    // to it), each a little earlier or later than the reflex's mean (15 ms either way).
    lead_seed_ = draw() > 0 ? 1 : -1;
    arm_gap_ = 0.02f + 0.04f * std::abs(draw());
    arm_jitter_ = {0.015f * draw(), 0.015f * draw()};
    // He decides to kick the board 170 to 300 ms into the bail; going down face first his head turns one way or the other;
    // racked he rolls over one way or the other; one side curls a little more than the other.
    kick_onset_ = 0.17f + 0.13f * std::abs(draw());
    turn_side_ = draw() > 0 ? 1 : -1;
    roll_side_ = draw() > 0 ? 1 : -1;
    rock_phase_ = pi * draw();
    asym_ = 0.3f * draw();
    const auto& b = in.body;
    for (auto& r : rest_) r = identity3();
    for (std::size_t i = 1; i < part_count; ++i)
        if (b[i].ok && b[parent_of[i]].ok && finite(b[i].r) && finite(b[parent_of[i]].r)) rest_[i] = multiply(transposed(b[parent_of[i]].r), b[i].r);
    for (auto& m : base_) m = identity3();
    landed_at_.fill(-10);
    limb_hit_at_.fill(-10);
    region_at_.fill(-10);
    overshot_at_.fill(-10);
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
    const float heavy = fin(in.tune.weight, 0.0f, 2.0f, 1.0f);

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
        if (!b[i].touching || b[i].on_board) continue;
        if (b[i].has_normal && finite(b[i].normal) && std::abs(b[i].normal[1]) < 0.5f * length(b[i].normal)) continue; // a wall
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
    const float torso_spin = 0.5f * (length(b[pelvis].w) + length(b[chest3].w));
    const V3 trunk_w = (b[pelvis].w + b[chest3].w) * 0.5f;
    const bool going_down = phase_ == Phase::brace;
    const bool airborne = untouched_for_ > 0.2;

    // Where his momentum takes him, m/s along the ground: his centre's motion against his feet's (riding, both go
    // together; only what the bail adds counts: the board stopping dead throws him on, the board shooting out leaves him
    // behind it), the top of his trunk as it turns (about a metre up), and his lean (gravity takes him the way he leans).
    const V3 feet_v = (b[l_foot].v + b[r_foot].v) * 0.5f;
    const V3 momentum = flat(mass.velocity - feet_v) + flat(cross(trunk_w, world_up)) + flat(spine_up) * 1.5f;
    // Which way he goes over: once that is clear (he leans, it is over half a metre a second, or he is about to land) it
    // is kept, and then turns no faster than 4 rad/s, so the arms do not swap sides on a wobble.
    if (going_down) {
        const float size = length(momentum);
        if (size > 0.15f) {
            const V3 falling = momentum * (1.0f / size);
            if (!fall_known_) {
                if (tilt > 12 * deg || size > 0.5f || to_hit < 0.6f) {
                    fall_ = falling;
                    fall_known_ = true;
                }
            } else {
                fall_ = normalized(flat(turn_towards(fall_, falling, 4.0f * dt)), fall_);
            }
        }
    }
    const V3 heading = fall_known_ ? fall_ : length(momentum) > 0.15f ? normalized(momentum) : facing;
    // Forward, backward and to a side, blended (cos^2 of the heading against his facing), not picked.
    const float ahead = dot(heading, facing), lateral = dot(heading, to_left);
    const float w_fwd = ahead > 0 ? ahead * ahead : 0.0f, w_back = ahead < 0 ? ahead * ahead : 0.0f, w_side = lateral * lateral;
    const float drop_down = -mass.velocity[1];
    // The kind of fall, with a little hysteresis: a new kind has to hold for 80 ms. Tumbling in the air (the trunk turning
    // over 6 rad/s) and dropping (falling over 2 m/s with little way to it and time before landing) are their own.
    if (going_down) {
        Fall want = Fall::unknown;
        if (length(trunk_w) > 6.0f && untouched_for_ > 0.1) want = Fall::spin;
        else if (drop_down > 2.0f && length(momentum) < 1.5f && to_hit > 0.35f && untouched_for_ > 0.1) want = Fall::drop;
        else if (fall_known_) want = ahead > 0.7071f ? Fall::forward : ahead < -0.7071f ? Fall::backward : lateral > 0 ? Fall::left : Fall::right;
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

    // ---- the hits ----------------------------------------------------------------------------------------------------
    // The way a contact pushed a body: the surface's normal, turned towards the body's centre (it is read either way
    // round); else from the contact to the centre; else up.
    const auto pushed = [&](std::size_t i) -> V3 {
        const V3 c = centre(i);
        const bool point = b[i].has_point && finite(b[i].point);
        if (b[i].has_normal && finite(b[i].normal) && length(b[i].normal) > 0.5f) {
            V3 n = normalized(b[i].normal);
            if (point ? dot(n, c - b[i].point) < 0 : n[1] < 0) n = -n;
            return n;
        }
        return point ? normalized(c - b[i].point, world_up) : world_up;
    };
    const auto hit_point = [&](std::size_t i) { return b[i].has_point && finite(b[i].point) ? b[i].point : centre(i); };
    // A startle is biggest the first time: it halves on a repeat and comes back over 3 to 5 s.
    startle_ += (1.0f - startle_) * (1.0f - std::exp(-dt / 4.0f));
    // Each arm or leg body that comes down hard on something lands heavily (its limb's thud, below).
    for (std::size_t i = 1; i < part_count; ++i) {
        const Group g = group_of(static_cast<Part>(i));
        if ((g == arm_left || g == arm_right || g == leg_left || g == leg_right) && b[i].touching && fin(b[i].impact, 0.0f, 50.0f, 0.0f) > 1.0f)
            landed_at_[i] = t_;
    }
    // A limb that is hit flinches (below): over 1 m/s along the contact's normal, as hard as 4 m/s and more makes it. Not
    // the feet (they land on purpose), nor a hand coming down to catch the fall.
    {
        static constexpr std::array<std::array<Part, 3>, 4> limbs{{{l_arm, l_forearm, l_hand}, {r_arm, r_forearm, r_hand},
                                                                   {l_thigh, l_shin, board}, {r_thigh, r_shin, board}}};
        for (std::size_t k = 0; k < 4; ++k)
            for (const Part p : limbs[k]) {
                if (p == board || !b[p].touching) continue;
                if (going_down && (p == l_hand || p == r_hand)) continue;
                const float size = std::clamp((fin(b[p].impact, 0.0f, 50.0f, 0.0f) - 1.0f) / 3.0f, 0.0f, 1.0f);
                if (size < 0.08f || (t_ - limb_hit_at_[k] < 0.3 && size < limb_hit_size_[k] + 0.15f)) continue;
                limb_hit_at_[k] = t_;
                limb_hit_size_[k] = size;
                limb_hit_from_[k] = pushed(p);
                limb_hit_point_[k] = hit_point(p);
                limb_hit_delay_[k] = 0.075f + 0.015f * draw(); // a withdrawal, 60 to 90 ms on
            }
        // A hit to the head or the upper body: the chin comes in (below).
        for (const Part p : {head, neck, chest3, chest2, back1, spine, pelvis, l_collar, r_collar}) {
            if (!b[p].touching) continue;
            const float size = std::clamp((fin(b[p].impact, 0.0f, 50.0f, 0.0f) - 1.0f) / 3.0f, 0.0f, 1.0f);
            if (size < 0.1f || (t_ - head_hit_at_ < 0.3 && size < head_hit_size_ + 0.15f)) continue;
            head_hit_at_ = t_;
            head_hit_size_ = size * startle_;
            startle_ *= 0.6f;
        }
    }
    if (torso_hit > 3.0f || upper_hit > 3.0f) last_hard_ = t_;
    // An arm that takes a hit itself flinches and then goes loose; a hand coming down is the catch.
    for (int s = 0; s < 2; ++s) {
        const auto i = static_cast<std::size_t>(s);
        float hit = 0;
        for (const Part p : s == 0 ? std::array<Part, 3>{l_arm, l_forearm, l_collar} : std::array<Part, 3>{r_arm, r_forearm, r_collar})
            if (b[p].touching) hit = std::max(hit, fin(b[p].impact, 0.0f, 50.0f, 0.0f));
        if (hit > 2.5f) arm_hit_at_[i] = t_;
        const Part hand = s ? r_hand : l_hand, fore = s ? r_forearm : l_forearm, upper = s ? r_arm : l_arm;
        if (going_down && caught_at_[i] < 0 && (b[hand].touching || b[fore].touching)) {
            caught_at_[i] = t_;
            caught_bend_[i] = bend_of(multiply(transposed(b[upper].r), b[fore].r));
        }
    }
    // Racked: astride something (a rail, a coping), his legs hanging down either side of it, and hit between them from
    // below: the pelvis's contact under its middle (towards the legs, not out at a hip, not on his seat), or both inner
    // thighs hit up near the top, close together.
    bool racked = false;
    float racked_speed = 0;
    {
        const bool astride = dot(b[l_thigh].r[0], world_up) < -0.4f && dot(b[r_thigh].r[0], world_up) < -0.4f &&
                             b[l_foot].p[1] < b[pelvis].p[1] - 0.45f && b[r_foot].p[1] < b[pelvis].p[1] - 0.45f;
        const auto& pv = b[pelvis];
        const float pelvis_hit = pv.touching ? fin(pv.impact, 0.0f, 50.0f, 0.0f) : 0.0f;
        if (astride && pelvis_hit > 1.0f) {
            if (pv.has_point && finite(pv.point)) {
                const V3 at = turn_back(pv.r, pv.point - centre(pelvis)); // x up his spine, y forwards, z to his left
                const bool from_above = !pv.has_normal || std::abs(normalized(pv.normal)[1]) > 0.5f;
                racked = at[0] < -0.03f && std::abs(at[2]) < 0.09f && at[1] > -0.07f && from_above;
            } else {
                racked = dot(spine_up, world_up) > 0.6f; // sat down hard on it upright, the legs hanging: no other way
            }
            if (racked) racked_speed = pelvis_hit;
        }
        if (astride && !racked && b[l_thigh].touching && b[r_thigh].touching && b[l_thigh].has_point && b[r_thigh].has_point) {
            const V3 lp = b[l_thigh].point, rp = b[r_thigh].point;
            const V3 la = turn_back(b[l_thigh].r, lp - b[l_thigh].p), ra = turn_back(b[r_thigh].r, rp - b[r_thigh].p);
            // A leg's z is his left: the left thigh's inner side is its -z, the right's its +z.
            const float speed = std::max(fin(b[l_thigh].impact, 0.0f, 50.0f, 0.0f), fin(b[r_thigh].impact, 0.0f, 50.0f, 0.0f));
            if (finite(lp) && finite(rp) && length(lp - rp) < 0.2f && la[0] < 0.2f && ra[0] < 0.2f && la[2] < 0.0f && ra[2] > 0.0f && speed > 1.0f) {
                racked = true;
                racked_speed = speed;
            }
        }
    }
    // What hurts, remembered by place (the head, each arm, hand and leg, each hip and side of the ribs, the back, the
    // groin), for what he does once he is down: each new hit adds to its place's hurt, and it eases off over the bail
    // (halving in about ten seconds), so two knocks on one elbow can hurt more than one harder knock on the hip. A hit
    // counts once: when the body comes down on something after a tenth of a second off it (a contact that flickers is the
    // same one) or is struck 1 m/s harder than so far while it touches; lying or sliding on it adds nothing. Under 2 m/s a
    // flinch at most, nothing to hold. Each hit adds how much more than 1.5 m/s it was, times its place's share
    // (site_factor); a hand that came down to catch the fall counts under half (it meant to land); racked counts most of
    // all, however soft. Where on the body each place was last hit is kept (the hand goes to that spot).
    {
        const float fade = std::exp(-dt / 15.0f);
        for (auto& d : damage_) d *= fade;
        const auto add = [&](Hurt h, Part p, float speed, float amount) {
            const auto k = static_cast<std::size_t>(h);
            damage_[k] += amount;
            region_part_[k] = p;
            region_known_[k] = b[p].has_point && finite(b[p].point);
            region_local_[k] = region_known_[k] ? turn_back(b[p].r, b[p].point - b[p].p) : V3{};
            region_at_[k] = t_;
            region_speed_[k] = std::max(region_speed_[k], speed);
        };
        for (std::size_t i = 1; i < part_count; ++i) {
            const auto p = static_cast<Part>(i);
            const float speed = b[i].touching ? fin(b[i].impact, 0.0f, 50.0f, 0.0f) : 0.0f;
            off_for_[i] = b[i].touching ? 0.0f : off_for_[i] + dt;
            const bool landed = b[i].touching && off_for_[i] == 0.0f && was_off_[i];
            const bool fresh = b[i].touching && (landed || speed > last_impact_[i] + 1.0f);
            if (landed) last_impact_[i] = 0;
            was_off_[i] = !b[i].touching ? off_for_[i] >= 0.1f || was_off_[i] : false;
            if (b[i].touching) last_impact_[i] = std::max(last_impact_[i], speed);
            if (!fresh || is_foot(p) || speed < 2.0f) continue;
            const V3 local = b[i].has_point && finite(b[i].point) ? turn_back(b[i].r, b[i].point - centre(i)) : V3{};
            const Hurt h = hurt_of(p, local, b[i].has_point && finite(b[i].point), roll_side_ > 0);
            if (h == Hurt::groin || (racked && (p == pelvis || p == l_thigh || p == r_thigh))) continue; // racked: below
            const auto caught_just = [&](int s) {
                const double at = caught_at_[static_cast<std::size_t>(s)];
                return at >= 0.05 && t_ - at < 0.35;
            };
            const bool catching = (p == l_hand && caught_at_[0] >= 0) || (p == r_hand && caught_at_[1] >= 0) ||
                                  ((p == l_forearm || p == l_arm) && caught_just(0)) || ((p == r_forearm || p == r_arm) && caught_just(1));
            add(h, p, speed, (speed - 1.5f) * site_factor(h) * (catching ? 0.45f : 1.0f));
        }
        if (racked && t_ - region_at_[static_cast<std::size_t>(Hurt::groin)] > 0.3)
            add(Hurt::groin, pelvis, racked_speed, std::max((racked_speed - 1.0f) * site_factor(Hurt::groin), 5.0f));
        // What he holds: the place that hurts most now, kept unless another hurts clearly more (a quarter more).
        std::size_t most = 0;
        for (std::size_t k = 1; k < damage_.size(); ++k)
            if (damage_[k] > damage_[most]) most = k;
        const auto now = static_cast<std::size_t>(hurt_);
        if (damage_[most] > 0.3f && most != now && (hurt_ == Hurt::none || damage_[most] > 1.25f * damage_[now] + 0.2f)) {
            const auto h = static_cast<Hurt>(most);
            hurt_ = h;
            hurt_part_ = region_part_[most];
            hurt_local_known_ = region_known_[most];
            hurt_local_ = region_local_[most];
            hurt_speed_ = region_speed_[most];
            hurt_at_ = region_at_[most];
            held_ = 0;
            // The hurt part is guarded at once (the withdrawal, 60 to 120 ms after the hit: a hurt arm drawn in, a knee
            // drawn up); the hands set off after he has taken it in (Euphoria's 0.2 s: hand_delay_ below), so a hand is on
            // the spot 0.6 to 1 s after the hit. Racked, at once; a hard knock on the head leaves him stunned a moment
            // longer first. (Waiting longer is waiting for the limbs to stop, and the game holds a limb that has stopped.)
            hurt_delay_ = h == Hurt::groin ? 0.06f : 0.08f + 0.04f * std::abs(draw());
            if (h == Hurt::head && hurt_speed_ > 5.0f) hurt_delay_ += 0.25f;
            pain_ = false; // something else hurts more: the reaction starts again, for it
        } else if (hurt_ != Hurt::none) {
            // Hit there again: the spot moves to the new hit.
            const auto k = static_cast<std::size_t>(hurt_);
            hurt_part_ = region_part_[k];
            hurt_local_known_ = region_known_[k];
            hurt_local_ = region_local_[k];
            hurt_speed_ = region_speed_[k];
        }
    }
    out.hurt = hurt_;
    out.hurt_amount = damage_[static_cast<std::size_t>(hurt_)];

    // ---- the phase -----------------------------------------------------------------------------------------------
    // Hit: stunned for a moment, as long and as deep as the hit (0.15 s and a little for a soft landing, up to 0.6 s and
    // most of the tone for a slam), so the body goes wherever the momentum sends it; and weaker for a moment (down to a
    // half of his strength, back over 0.4 s: Euphoria's weakness at a hit).
    const auto stun = [&](float speed) {
        stun_until_ = t_ + std::clamp(0.15f + 0.1f * (speed - 1.5f), 0.15f, 0.6f);
        stun_depth_ = std::clamp(0.3f + 0.12f * (speed - 1.5f), 0.3f, 0.9f);
        last_stun_ = t_;
        weak_at_ = t_;
        weak_depth_ = std::clamp(0.2f + 0.1f * (speed - 1.5f), 0.2f, 0.5f);
    };
    // Lying still: his torso down, and the whole of him nearly at rest (under about 0.45 m/s on average, hardly turning);
    // moving again is well over that (the gap keeps him from flickering between the two).
    const float energy = mass.energy / mass.total; // J per kg: half the mean square speed
    const bool on_ground = torso_off_for_ < 0.25 && !airborne;
    still_ = still_ ? on_ground && energy < 0.4f && torso_spin < 2.0f : on_ground && energy < 0.1f && torso_spin < 1.0f;
    still_for_ = still_ ? still_for_ + dt : 0;
    const bool falling_again = airborne && mass.velocity[1] < -1.5f;
    const bool hit = (torso_touch && torso_hit > 1.2f) || upper_hit > 3.0f || torso_down_for_ > 0.06;
    const bool hurt_on = in.tune.grab_hurt && hurt_ != Hurt::none;
    const bool racked_now = hurt_ == Hurt::groin;
    // How bad it is: what that place has taken, against one hit of 5.5 m/s there; how long he holds it: about Hold what
    // hurts for, more for a harder hit and for what hurts more (racked longest).
    const float severity = hurt_ == Hurt::none ? 0.0f
                                               : std::clamp(damage_[static_cast<std::size_t>(hurt_)] / (4.0f * site_factor(hurt_)), 0.0f, 1.0f);
    const float hold_for = std::clamp(hold_hurt / 4.5f * hold_base(hurt_) * (0.5f + (racked_now ? 1.0f : severity)), 1.0f, 15.0f);
    const auto moving = [&] { return torso_spin > 4.5f ? Phase::tumble : Phase::slide; };
    switch (phase_) {
    case Phase::brace:
    case Phase::stumble:
        if (hit) {
            stun(std::max(torso_hit, upper_hit));
            if (first_hit_at_ < 0) first_hit_at_ = t_;
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
        else if (in_phase_ > lie_still && (!hurt_on || held_ >= hold_for)) enter(Phase::rest);
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
    const bool fell = first_hit_at_ >= 0 && phase_ != Phase::brace; // he has come down (the arms' catch is over)

    // Down: on something with his trunk (the ground, or the rail he is astride), not tumbling, flying or sliding fast, and
    // no hard landing coming in the next 0.3 s. Slow enough under 3 m/s and 4 rad/s of the trunk, and fast again over
    // 4.5 m/s or 6 rad/s: he reacts while he still slides (real skaters grab what hurts while they slide; and the game
    // holds a limb that has stopped turning, so a reach that waited until he lay still could no longer start). Racked he
    // holds on even falling off the rail.
    {
        const float speed = length(mass.velocity);
        slow_ = slow_ ? speed < 4.5f && torso_spin < 6.0f : speed < 3.0f && torso_spin < 4.0f;
        const bool landing_soon = pred.hit && pred.t < 0.3f && out.hit_speed > 2.5f;
        down_ = racked_now ? slow_ : slow_ && torso_off_for_ < 0.25 && !airborne && !landing_soon && fell;
        down_for_ = down_ ? down_for_ + dt : 0;
    }
    out.down = down_;
    // The reaction to what hurts: once he is down, a moment after the hit, for about as long as Hold what hurts for says;
    // never while he tumbles, flies or slides fast (it stops at once, and starts again once he is slow again), nor going
    // down again (the arms catch first).
    const bool pain = hurt_on && down_ && t_ - hurt_at_ >= hurt_delay_ / reaction && held_ < hold_for && (racked_now || phase_ != Phase::brace);
    if (pain && !pain_) {
        for (std::size_t k = 0; k < joint_count; ++k) {
            const auto& l = ranges()[k];
            base_[k] = multiply(transposed(b[l.frame].r), b[l.child].r);
        }
        pain_since_ = t_;
        let_go_at_ = -1;
        reach_ = {Reach::none, Reach::none};
        on_spot_at_ = {-1, -1};
        best_gap_ = {1e9f, 1e9f};
        nearer_at_ = {t_, t_};
        // The nearer hand sets off a tenth of a second after the guarding starts (about 0.2 s after the hit), the other
        // (when both go) 0.15 to 0.35 s after it; a hurt arm's own hand is drawn in at once (each reaction below).
        const float first = 0.1f, second = first + 0.15f + 0.2f * std::abs(draw());
        hand_delay_ = {first, first};
        if (hurt_part_ != board) {
            const V3 spot = hurt_local_known_ ? b[hurt_part_].p + turn_by(b[hurt_part_].r, hurt_local_) : centre(hurt_part_);
            const bool left_nearer = length(b[l_hand].p - spot) <= length(b[r_hand].p - spot);
            hand_delay_[left_nearer ? 1 : 0] = second;
        } else {
            hand_delay_[roll_side_ > 0 ? 1 : 0] = second;
        }
    }
    // Held for as long as it lasts and still down: he lets go (the rhythm stops; the hands stay where they lie, as a
    // person's do).
    // Held for as long as it lasts: he lets go over a second and a half (the rhythm stops at once, the hands and the curl
    // ease off), never at once (an arm let drop all at once reads as fainting). Moving fast again ends it at once.
    if (pain_ && !pain && down_ && held_ >= hold_for) let_go_at_ = t_;
    if (!down_ || (let_go_at_ >= 0 && t_ - let_go_at_ > 1.5)) let_go_at_ = -1;
    const bool letting = !pain && let_go_at_ >= 0;
    const float fade = letting ? 1.0f - smooth(static_cast<float>(t_ - let_go_at_) / 1.5f) : 1.0f;
    pain_ = pain;
    if (pain) held_ += dt;
    if (!pain && !letting) reach_ = {Reach::none, Reach::none};
    out.pain = pain;

    // ---- what each joint is asked ----------------------------------------------------------------------------------
    // Tone, by group, for the phase. The neck's is Head protection's, and it never drops below 0.2 unless he is stunned
    // (a head that lolls reads as knocked out). Once he is down the arms are kept loose (they have a reach to make, and a
    // limb the muscles slow to a stop is one the game then holds).
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
    case Phase::tumble: tone = {0.12f, 0.25f, 0.08f, 0.08f, 0.08f, 0.08f}; break;
    case Phase::slide: tone = {0.1f, 0.2f, 0.03f, 0.03f, 0.06f, 0.06f}; break;
    case Phase::settle: {
        const float f = std::clamp(static_cast<float>(in_phase_) / lie_still, 0.0f, 1.0f);
        tone = {lerp(0.08f, 0.03f, f), lerp(0.15f, 0.04f, f), 0.03f, 0.03f, lerp(0.05f, 0.025f, f), lerp(0.05f, 0.025f, f)};
        break;
    }
    case Phase::rest: tone = {0.025f, 0.03f, 0.02f, 0.02f, 0.02f, 0.02f}; break;
    case Phase::idle: break;
    }
    tone[neck_group] = std::max(tone[neck_group], (phase_ == Phase::rest ? 0.12f : 0.2f) * (1 - stunned)) * head_k;
    // Heavy limbs: the arms' and legs' tone, so they swing with the weight of an arm or a leg instead of flopping (half
    // of it at 0, 1.4 times as tuned, 2.3 times at 200%).
    for (const Group g : {arm_left, arm_right, leg_left, leg_right}) tone[g] *= 0.5f + 0.9f * heavy;
    // A limb that has just come down hard lands heavily: for a fifth of a second its joints, from where it landed up to
    // the shoulder or hip, soak the landing up, so the limb stays where it lands instead of bouncing or flopping on.
    std::array<bool, part_count> thud{};
    for (std::size_t i = 1; i < part_count; ++i)
        if (t_ - landed_at_[i] < 0.2)
            for (Part c = static_cast<Part>(i); c != pelvis && c != l_collar && c != r_collar && group_of(c) != trunk && group_of(c) != neck_group;
                 c = parent_of[c])
                thud[c] = true;
    std::array<Drive, joint_count> drive{};
    const auto damper = [&](float damp) { return 1.0f - std::exp(-damp * dt); };
    for (std::size_t k = 0; k < joint_count; ++k) {
        auto& d = drive[k];
        const Part c = ranges()[k].child;
        d.u = tone[group_of(c)];
        // Tense muscle resists being turned fast: from about 1/s (lying there) to 3.5/s (falling, tense).
        d.share = damper(1.0f + 12.0f * d.u * strength);
        if (thud[c] && heavy > 0.01f) {
            d.u = std::max(d.u, 0.25f * heavy);
            d.share = std::max(d.share, damper((4.0f + 4.0f * heavy) * strength));
        }
    }
    const auto drive_of = [&](Part c) -> Drive& { return drive[index_of()[c]]; };
    const auto frame_of = [&](Part c) -> const M3& { return b[ranges()[index_of()[c]].frame].r; };
    const auto rel_of = [&](Part c) { return multiply(transposed(frame_of(c)), b[c].r); };
    const auto turning = [&](Part c) { return b[c].w - b[ranges()[index_of()[c]].frame].w; };
    // A move: the joint pushed along its way to a goal (`target`, its turning: world), firmly (`share` of the gap a step),
    // with up to `u` of its strength, holding up `carry` of the weight beyond it.
    // A reflex speeds a joint up as fast as muscle can (a shoulder about 80 rad/s^2); a move he means, lying there, about
    // half of that (a reach to one's own body peaks at 1.5-2 m/s at the hand about 0.2 s in).
    float voluntary = 0;
    float easing = 1; // letting go of what hurts: the moves' strength easing off
    const auto move = [&](Part c, const V3& target, float share, float u, float carry = 0.0f) {
        auto& d = drive_of(c);
        d.goal = true;
        d.accel = voluntary;
        d.target = target;
        d.share = share * easing;
        d.u = std::max(d.u, u * easing);
        d.carry = std::max(d.carry, carry * easing);
    };
    // A ball joint to a goal (the child's axes in the world), the goal kept inside its range.
    const auto move_to = [&](Part c, const M3& want, const Pace& pace, float share, float u, float carry = 0.0f) {
        const auto& l = ranges()[index_of()[c]];
        const M3 inside = limits::clamp_target(l, b[l.frame].r, want, rest_);
        move(c, seek(b[c].r, inside, pace), share, u, carry);
    };
    // A hinge to a bend (an elbow 0 to 145 degrees, a knee 10 to 150, kept 8 inside).
    const auto bend_to = [&](Part c, float want, const Pace& pace, float share, float u, float carry = 0.0f) {
        const auto& l = ranges()[index_of()[c]];
        const float goal = std::clamp(want, l.r[2].lo + 8 * deg, l.r[2].hi - 8 * deg);
        move(c, tree.axis[c] * seek_bend(bend_of(rel_of(c)), goal, pace), share, u, carry);
    };
    // A ball joint's bone swung to point along `dir` (world), its twist left as it is.
    const auto swing_to = [&](Part c, const V3& dir) {
        const V3 now = normalized(b[c].r[0]);
        return multiply(rotation(swing_between(now, normalized(dir, now))), b[c].r);
    };

    // ---- reflexes to the hits ----------------------------------------------------------------------------------------
    // A limb that is hit tenses at once (a stretch reflex, 25 ms on, for a tenth of a second or two), then pulls back from
    // it (a withdrawal, 60 to 90 ms on, for a fifth of a second): its elbow or knee bends and the whole limb turns away
    // from what hit it, at up to 4.5 rad/s (15 to 30 degrees in all). Each is a push towards a speed, never a pose: once
    // it is over the limb is as loose as it was. A leg is never pulled up by it once he is down.
    float flinch_most = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        const double since = t_ - limb_hit_at_[k];
        if (since > 0.7) continue;
        const float s = limb_hit_size_[k];
        const float tense = burst(since, 0.025f, 0.015f, 0.06f + 0.1f * s, 0.06f);
        const float pull = burst(since, limb_hit_delay_[k], 0.04f, 0.05f + 0.1f * s, 0.08f);
        flinch_most = std::max(flinch_most, std::max(tense, pull) * s);
        const bool arm = k < 2;
        const Part root = arm ? (k ? r_arm : l_arm) : (k == 2 ? l_thigh : r_thigh);
        const Part bend = arm ? (k ? r_forearm : l_forearm) : (k == 2 ? l_shin : r_shin);
        const Part end = arm ? (k ? r_hand : l_hand) : (k == 2 ? l_foot : r_foot);
        const float most = std::max(tense, pull);
        if (most <= 0.01f) continue;
        for (const Part c : {root, bend, end}) {
            auto& d = drive_of(c);
            d.u = std::max(d.u, (0.15f + 0.3f * s) * tense + (0.3f + 0.3f * s) * pull);
            d.share = std::max(d.share, damper((3.0f + 9.0f * s * most) * strength));
            out.reflex[c] = true;
        }
        const bool may_pull = arm || ((phase_ == Phase::brace || phase_ == Phase::impact || phase_ == Phase::tumble) && !down_);
        if (pull > 0 && may_pull) {
            drive_of(bend).target = tree.axis[bend] * ((2.0f + 4.0f * s) * pull);
            const V3 r = limb_hit_point_[k] - b[root].p;
            drive_of(root).target = normalized(cross(r, limb_hit_from_[k]), V3{}) * ((1.5f + 3.0f * s) * pull);
            for (const Part c : {root, bend}) drive_of(c).share = std::max(drive_of(c).share, 0.35f * pull);
        }
    }
    // A hit to the head or the upper body: the chin comes in (the neck's reflex to a jolt, 30 to 50 ms on), the neck
    // stiffening with it, and the back curls a touch (a startle), for a fifth to a third of a second, as hard as Head
    // protection says for the neck. Going down face first the chin comes in only half as far. Repeated hits startle less.
    {
        const double since = t_ - head_hit_at_;
        const float s = head_hit_size_;
        const float e = since < 1.0 ? burst(since, 0.035f, 0.03f, 0.08f + 0.12f * s, 0.12f) : 0.0f;
        if (e > 0.01f) {
            flinch_most = std::max(flinch_most, e * s);
            const float face_first = kind_ == Fall::forward ? 0.5f : 1.0f;
            if (head_k > 0.01f)
                for (const Part p : {neck, head}) {
                    auto& d = drive_of(p);
                    d.u = std::max(d.u, (0.4f + 0.3f * s) * e * std::min(head_k, 1.0f));
                    d.share = std::max(d.share, 0.3f * e);
                    d.target = frame_of(p)[2] * ((1.5f + 2.5f * s) * e * face_first * (p == neck ? 1.0f : 0.5f));
                    out.reflex[p] = true;
                }
            for (const Part p : {spine, back1, chest2, chest3}) {
                auto& d = drive_of(p);
                d.u = std::max(d.u, 0.2f * e);
                d.share = std::max(d.share, 0.15f * e);
                d.target = d.target + frame_of(p)[2] * ((0.3f + 0.4f * s) * e);
                out.reflex[p] = true;
            }
        }
    }
    out.flinch = flinch_most;

    // ---- head protection -------------------------------------------------------------------------------------------
    // From the start of the fall (60 to 100 ms on, nearly there in a quarter of a second), through every hit and tumble:
    // the chin to the chest, as hard as Head protection says. Going over backwards most (40 degrees), sideways with the
    // head bent away from the ground too, dropping, and tumbling (45). Going down face first a tuck would put his face in
    // first: there the head turns instead (the cheek to the ground, the chin a little up), as far as Head protection says.
    // A neck's strength is all it has (half to three quarters of it at 100%): a big enough slam still throws the head.
    {
        float want_chin = 0, want_side = 0, want_turn = 0, want_lift = 0, u = 0;
        const bool guarding = (phase_ == Phase::brace || phase_ == Phase::impact || phase_ == Phase::tumble) && t * reaction >= neck_latency &&
                              head_k > 0.01f && !pain;
        if (guarding) {
            if (phase_ == Phase::tumble || kind_ == Fall::spin) {
                want_chin = 45 * deg;
                u = 0.7f;
            } else if (kind_ == Fall::drop) {
                want_chin = 35 * deg;
                u = 0.5f;
            } else if (w_fwd > 0.5f) {
                want_turn = 40 * deg * w_fwd * std::min(head_k, 1.5f);
                want_lift = 15 * deg * w_fwd;
                want_chin = (35 * deg * w_back + 20 * deg * w_side);
                u = 0.5f;
            } else {
                want_chin = 40 * deg * w_back + 20 * deg * w_side + 25 * deg * w_fwd;
                want_side = (lateral > 0 ? 1.0f : -1.0f) * 25 * deg * w_side; // away from the ground: +y bends him to his right
                u = 0.6f * w_back + 0.55f * w_side + 0.4f * w_fwd;
            }
            if (lateral > 0.2f) turn_side_ = -1; // going down to his left: the face turns to his right, the left cheek down
            else if (lateral < -0.2f) turn_side_ = 1;
        }
        // Eased in (about 90% in a quarter of a second), and let go quickly (that only leaves the neck to its tone).
        const auto ease = [&](float now, float want) { return now + std::clamp(want - now, -6.0f * dt, 3.5f * dt); };
        chin_ = ease(chin_, want_chin);
        turn_ = ease(turn_, want_turn);
        lift_ = ease(lift_, want_lift);
        side_ = ease(side_, want_side);
        if (std::abs(chin_) + std::abs(turn_) + std::abs(lift_) + std::abs(side_) > 1e-3f && guarding) {
            const float hard = std::min(u * head_k * (phase_ == Phase::impact ? 1.3f : 1.0f), 0.9f);
            const float turn = turn_ * static_cast<float>(turn_side_);
            const Pace pace{4.0f * std::sqrt(std::min(head_k, 1.5f)), 30, 0.6f, 2 * deg};
            // (Its pace is a neck's already: limited by how fast the head may be turned as a whole, not joint by joint, or
            // the head's own turn takes the neck's back out of it.)
            voluntary = 200.0f;
            for (const Part p : {neck, head}) {
                const bool lower = p == neck;
                const M3 want = multiply(frame_of(p), rotation(V3{turn * (lower ? 0.4f : 0.6f), side_ * (lower ? 0.65f : 0.35f),
                                                                  chin_ * (lower ? 0.65f : 0.35f) - lift_ * (lower ? 0.6f : 0.4f)}));
                move_to(p, want, pace, 0.45f, hard);
            }
            voluntary = 0;
        }
    }

    // ---- the arms going down --------------------------------------------------------------------------------------
    // From the moment the bail starts, the arms reach the way his momentum takes him: forwards, back and down, or out to
    // the side he goes (blended), for where the ground will be when he gets there. The arm on the side he falls to leads,
    // 20 to 60 ms before the other, and reaches further. Short of time (under 0.45 s to the hit) they still go, but get
    // only part of the way: bent, nearer the chest (a startle). Tumbling through the air they frame the head; dropping
    // they go out to the sides, then forwards and down for the landing. They move at a quick reach's pace (up to 8-12 rad/s
    // at the shoulder), and the weight of the arm is the fall's (no help against gravity while falling).
    if (lead_ == 0 && t * reaction >= arm_latency) {
        lead_ = std::abs(lateral) > 0.35f ? (lateral > 0 ? -1 : 1) : lead_seed_;
        arm_delay_ = lead_ < 0 ? std::array<float, 2>{0.0f, arm_gap_} : std::array<float, 2>{arm_gap_, 0.0f};
    }
    for (int s = 0; s < 2; ++s) {
        const auto i = static_cast<std::size_t>(s);
        const Part upper = s ? r_arm : l_arm, fore = s ? r_forearm : l_forearm, hand = s ? r_hand : l_hand;
        const float out_z = s == 0 ? 1.0f : -1.0f; // this arm's outside, as the chest's z
        const bool lead = lead_ != 0 && (lead_ < 0) == (s == 0);
        const float arm_len = length(b[fore].p - b[upper].p) + length(b[hand].p - b[fore].p) + 0.08f;
        out.hand_mark[i] = V3{std::nanf(""), std::nanf(""), std::nanf("")}; // aiming nowhere
        if (phase_ == Phase::brace || phase_ == Phase::impact) {
            const double since_catch = caught_at_[i] >= 0 ? t_ - caught_at_[i] : -1;
            if (t_ - arm_hit_at_[i] < 0.6 && since_catch < 0) {
                // Hit itself: it flinches (above), then it is loose.
                if (t_ - arm_hit_at_[i] > 0.35)
                    for (const Part p : {upper, fore, hand}) drive_of(p).u = std::min(drive_of(p).u, 0.05f);
                continue;
            }
            if (since_catch >= 0) {
                // Down on the ground: the elbow gives, bending to about 100 degrees over a quarter of a second against high
                // damping (a bent elbow takes 40% off the hand's hardest push), the shoulder resisting; then the arm is as
                // loose as the rest of him.
                auto& sh = drive_of(upper);
                if (since_catch < 0.3) {
                    sh.u = std::max(sh.u, 0.45f);
                    sh.share = std::max(sh.share, damper(8.0f * strength));
                    const float to = lerp(caught_bend_[i], 100 * deg, smooth(static_cast<float>(since_catch) / 0.25f));
                    bend_to(fore, to, Pace{5, 40, 0.8f, 3 * deg}, 0.3f, 0.4f);
                } else {
                    auto& el = drive_of(fore);
                    sh.u = el.u = 0.06f;
                    sh.share = el.share = damper(1.5f);
                }
                continue;
            }
        }
        const float onset = (arm_latency + arm_delay_[i] + arm_jitter_[i]) / reaction;
        const bool reach = in.tune.catch_arms && going_down && lead_ != 0 && t >= onset && !(pain && racked_now);
        if (!reach) continue;
        // Little time before the hit: out quicker, and it arrives bent; with time it is long.
        const float urgency = std::clamp((0.7f - to_hit) / 0.5f, 0.0f, 1.0f);
        const float late = std::clamp((0.35f - to_hit) / 0.25f, 0.0f, 1.0f);
        amp_[i] = std::max(amp_[i], std::clamp((to_hit - 0.25f) / 0.2f, 0.5f, 1.0f));
        V3 dir{};
        float bend = (35 + 20 * late) * deg * (lead ? 0.75f : 1.0f), firm = 1.0f;
        if (kind_ == Fall::spin) {
            dir = normalized(V3{0.42f, 0.85f, 0.3f * out_z});
            bend = 130 * deg;
            firm = 0.8f;
        } else if (kind_ == Fall::drop) {
            const float landing = std::clamp((0.4f - to_hit) / 0.15f, 0.0f, 1.0f);
            dir = normalized(V3{lerp(-0.17f, -0.6f, landing), lerp(0.05f, 0.6f, landing), lerp(0.98f, 0.45f, landing) * out_z});
            bend = lerp(45, 30, landing) * deg;
        } else {
            // Set in the world (down to the ground, ahead of him, back and out, or to the side he goes) and read in the
            // upper chest's axes as they will be when he lands, so it is where the ground will be for the arm, not where
            // it is now. Sideways, the arm on the fall's side goes out to it; the high arm forwards.
            const V3 outward = to_left * out_z;
            const bool fall_side = (lateral > 0) == (s == 0);
            const V3 fwd = normalized(-world_up + heading * 0.7f + outward * 0.25f);
            const V3 back = normalized(-world_up + heading * 0.45f + outward * 0.7f);
            const V3 side = fall_side ? normalized(-world_up * 0.8f + heading * 0.8f + facing * 0.15f)
                                      : normalized(-world_up * 0.3f + facing * 0.8f + heading * 0.35f);
            const V3 world_dir = normalized(fwd * w_fwd + back * w_back + side * w_side, fwd);
            dir = turn_back(multiply(pred.turn, chest), world_dir);
            firm = w_fwd + w_back + w_side * (fall_side ? 1.15f : 0.6f);
        }
        if (lead) firm *= 1.15f;
        // Short of time, part of the way: from the startle (the upper arm forwards and out, the elbow bent, hands coming
        // up in front of the chest) towards the reach.
        if (amp_[i] < 1.0f && kind_ != Fall::spin) {
            const V3 startle = normalized(V3{-0.75f, 0.55f, 0.35f * out_z});
            const float apart = std::acos(std::clamp(dot(startle, dir), -1.0f, 1.0f));
            dir = turn_towards(startle, dir, apart * amp_[i]);
            bend = lerp(60 * deg, bend, amp_[i]);
        }
        aim_[i] = dir;
        const float ramp = smooth((t - onset) / 0.1f);
        const float u = std::min((0.45f + 0.15f * urgency) * firm, 0.8f);
        // (Ramped in over a tenth of a second, its firmness with it: a goal held at no pace would brake the arm.)
        const Pace shoulder_pace{(8.0f + 4.0f * urgency) * reaction * ramp, 70.0f * reaction, 1.2f * ramp, 4 * deg};
        move_to(upper, swing_to(upper, turn_by(chest, dir)), shoulder_pace, 0.5f * ramp, u * ramp);
        const Pace elbow_pace{10.0f * reaction * ramp, 80.0f * reaction, 1.2f * ramp, 3 * deg};
        // The elbow stiffens 100 to 130 ms before the hands land, ready to take it.
        const bool ready = to_hit < 0.13f && pred.hit;
        bend_to(fore, bend, elbow_pace, (ready ? 0.6f : 0.5f) * ramp, (ready ? std::max(u, 0.6f) : u) * ramp);
        drive_of(hand).u = std::max(drive_of(hand).u, 0.15f);
        out.hand_mark[i] = b[upper].p + turn_by(chest, dir) * arm_len;
    }

    for (int s = 0; s < 2; ++s) out.foot_mark[static_cast<std::size_t>(s)] = b[s ? r_foot : l_foot].p;

    // ---- the board ---------------------------------------------------------------------------------------------------
    // Going down with his feet still on the board, he kicks it away (decided 170 to 300 ms into the bail, once): the foot
    // on it, the one ahead the way it goes, pushes it out, the knee straightening in a tenth of a second or two; away from
    // where he lands. The board leaves him at 3 m/s unless it is going 2 m/s or more already. When the runtime can set the
    // board's speed, the board gets that much and the foot as much back (2.5 kg against his 80: about 0.1 m/s, nothing to
    // see); otherwise the foot pushes it through the game's own contact.
    for (int s = 0; s < 2; ++s) {
        const Part foot = s ? r_foot : l_foot, toe = s ? r_toe : l_toe;
        if ((b[foot].touching && b[foot].on_board) || (b[toe].touching && b[toe].on_board)) board_at_[static_cast<std::size_t>(s)] = t_;
    }
    if (!kick_tried_ && t * reaction >= kick_onset_) {
        kick_tried_ = true;
        if (in.tune.kick_board && going_down) {
            const V3 away = length(momentum) > 0.3f || fall_known_
                                ? -heading
                                : in.board.ok && length(flat(in.board.p - mass.com)) > 0.1f ? normalized(flat(in.board.p - mass.com)) : -facing;
            int leg = 0;
            float front = -1e9f;
            for (int s = 0; s < 2; ++s) {
                const Part foot = s ? r_foot : l_foot;
                if (t_ - board_at_[static_cast<std::size_t>(s)] > 0.1) continue;
                const float along = dot(b[foot].p - mass.com, away);
                if (along > front) {
                    front = along;
                    leg = s ? 1 : -1;
                }
            }
            const float leaving = in.board.ok ? dot(in.board.v - mass.velocity, away) : 0.0f;
            if (leg != 0 && leaving < 2.0f) {
                const Part foot = leg > 0 ? r_foot : l_foot;
                kick_leg_ = leg;
                kick_at_ = t_;
                kick_dir_ = away;
                kick_target_ = b[foot].p + away * 0.35f - world_up * 0.03f;
                board_dv_ = away * std::clamp(kick_speed - std::max(leaving, 0.0f), 0.0f, kick_speed);
            }
        }
    }
    const bool kicking = kick_leg_ != 0 && t_ - kick_at_ < kick_time;
    if (kicking) {
        const int s = kick_leg_ > 0 ? 1 : 0;
        const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin, foot = s ? r_foot : l_foot;
        const V3 hip = b[thigh].p;
        const float a = length(b[shin].p - hip), c = length(b[foot].p - b[shin].p);
        const V3 knee_at = reach_middle(hip, kick_target_, a, c, b[shin].p - hip);
        const Pace pace{12, 150, 2, 3 * deg};
        move_to(thigh, swing_to(thigh, knee_at - hip), pace, 0.6f, 0.75f);
        bend_to(shin, 20 * deg, pace, 0.6f, 0.75f);
        drive_of(foot).u = std::max(drive_of(foot).u, 0.3f);
        out.kick_leg = kick_leg_;
        out.foot_mark[static_cast<std::size_t>(s)] = kick_target_;
    }

    // ---- what hurts ----------------------------------------------------------------------------------------------------
    // Once he is down and slow (sliding is fine), a person brings what hurts to the hands and the hands go to it: the hurt
    // arm drawn in to the belly and the other hand to it, a hurt knee drawn up to the chest and both hands round it, both
    // hands to the head or to the groin, a hand pressed to a hip, the back or the ribs. One hand first (the nearer), the
    // other 0.2 to 0.45 s after it; each goes as a quick reach does (there in about half a second) at a pace the game lets
    // through, and stops on the spot, pressed a centimetre in, where the game's hold keeps it. The body curls a little over
    // it and deepens the curl after a second (the second, dull pain); a bruise is rubbed. A spot it cannot get to (no
    // nearer for 0.6 s) is given up and the arm put down. Moving fast again ends it at once.
    std::array<bool, part_count> no_crawl{};
    std::array<bool, 2> lifted{}, reaching{}, arm_busy{}, leg_busy{};
    for (int s = 0; s < 2; ++s) arm_busy[static_cast<std::size_t>(s)] = (phase_ == Phase::brace || phase_ == Phase::impact) && !fell;
    if (kicking) leg_busy[kick_leg_ > 0 ? 1 : 0] = true;
    voluntary = 40.0f * reaction;
    easing = fade;
    if (pain || letting) {
        const float since = static_cast<float>(t_ - pain_since_);
        const float quick = std::sqrt(reaction);
        // The curl: as far as Curl up when hurt says, further the harder the hit; deeper after a second.
        const float second_wave = 0.75f + 0.25f * smooth((since - 1.0f) / 0.5f);
        const float curl = tuck_k * (racked_now ? 1.0f : 0.5f + 0.5f * severity) * second_wave;
        const int hurt_side = side_of(hurt_);
        const bool rhythm = pain && held_ < hold_for;
        // A point on a body's surface near where it was hit, and its outward normal: on its bone `from`..`to` of the way
        // along it. When that side now faces the ground (he lies on it: no hand gets under him) the side of it towards
        // `hand_side` (+1 his left) and up a little instead.
        struct Spot {
            V3 at{}, out{};
        };
        const auto surface_spot = [&](Part p, float from, float to, int hand_side) {
            const Part next = child_of(p);
            const float len = next != board ? length(b[next].p - b[p].p) : 2 * body::shapes[p].along;
            const V3 x = b[p].r[0];
            const V3 hit_at = hurt_local_known_ && p == hurt_part_ ? b[p].p + turn_by(b[p].r, hurt_local_) : centre(p) + world_up * 0.1f;
            const V3 on_bone = b[p].p + x * std::clamp(dot(hit_at - b[p].p, x), from * len, to * len);
            V3 radial = across(hit_at - on_bone, x);
            if (length(radial) < 0.01f || dot(normalized(radial), world_up) < -0.35f)
                radial = across(chest[2] * static_cast<float>(hand_side) + world_up * 0.6f, x);
            const V3 n = normalized(radial, world_up);
            return Spot{on_bone + n * std::max(body::shapes[p].radius, 0.02f), n};
        };
        // A hand to a spot (world, with its outward normal): the arm the two-bone way, the elbow out to his side and down
        // (lying there it rests on the ground, never pointing up into the air), the upper arm's whole turn so the elbow
        // folds the forearm onto the spot. It holds up the arm's weight on the way (a person's arm is lifted, not
        // dragged); once there it presses a centimetre in. `rub`: the spot moves to and fro along `along` (a bruise).
        const auto hand_to = [&](int s, Spot spot, float u, V3 along = {}) {
            const auto i = static_cast<std::size_t>(s);
            const Part upper = s ? r_arm : l_arm, fore = s ? r_forearm : l_forearm, hand = s ? r_hand : l_hand;
            if (since < hand_delay_[i] / quick) return;
            // A hand pinned under him (on the ground, under his chest) stays there: pulling it out would only push him
            // along the ground.
            {
                const V3 at = centre(hand), a0 = flat(centre(pelvis)), a1 = flat(centre(chest3));
                const V3 axis = a1 - a0;
                const float f = std::clamp(dot(flat(at) - a0, axis) / std::max(dot(axis, axis), 1e-4f), 0.0f, 1.0f);
                const bool under = length(flat(at) - (a0 + axis * f)) < 0.18f && at[1] < centre(chest2)[1] - 0.03f;
                if ((b[hand].touching || b[fore].touching) && under) {
                    reach_[i] = Reach::resting;
                    arm_busy[i] = true;
                    return;
                }
            }
            arm_busy[i] = true;
            const V3 palm = b[hand].p + b[hand].r[0] * 0.07f;
            const float gap = length(palm - spot.at);
            out.reach_gap[i] = gap;
            out.hand_mark[i] = spot.at;
            // Where it stands: on its way, there (within 4 cm; it lets go of the spot again past 8 cm: a re-grip), or
            // given up (no nearer by a centimetre for 0.6 s, and still 6 cm or more off).
            if (reach_[i] == Reach::none || reach_[i] == Reach::resting) {
                reach_[i] = Reach::going;
                best_gap_[i] = gap;
                nearer_at_[i] = t_;
            }
            if (gap < best_gap_[i] - 0.01f) {
                best_gap_[i] = gap;
                nearer_at_[i] = t_;
            }
            if (reach_[i] == Reach::going && gap < 0.04f) {
                reach_[i] = Reach::on_spot;
                on_spot_at_[i] = t_;
            } else if (reach_[i] == Reach::on_spot && gap > 0.08f) {
                reach_[i] = Reach::going;
                nearer_at_[i] = t_;
                best_gap_[i] = gap;
            } else if (reach_[i] == Reach::going && t_ - nearer_at_[i] > 0.6 && gap > 0.06f) {
                reach_[i] = Reach::gave_up;
                gave_up_at_[i] = t_;
            }
            if (reach_[i] == Reach::gave_up) {
                // It eases off over half a second (an arm let go of all at once drops like a fainting man's), then it is
                // put down below like any arm left up.
                const float left = 1.0f - smooth(static_cast<float>(t_ - gave_up_at_[i]) / 0.5f);
                if (left > 0.01f) {
                    move(upper, V3{}, 0.3f * left, u * left, left);
                    move(fore, V3{}, 0.3f * left, 0.85f * u * left, left);
                    reaching[i] = true;
                } else {
                    arm_busy[i] = false;
                }
                return;
            }
            reaching[i] = true;
            const bool on = reach_[i] == Reach::on_spot;
            V3 aim = spot.at - spot.out * 0.01f;
            if (on && rhythm && length(along) > 0.5f) {
                // Rubbing it: 3 cm to and fro, about 1.6 times a second, in bursts.
                const float burst_on = std::sin(2 * pi * 0.25f * t + rock_phase_) > -0.3f ? 1.0f : 0.0f;
                aim = aim + normalized(along) * (0.03f * burst_on * std::sin(2 * pi * 1.6f * t + rock_phase_));
            }
            const V3 shoulder = b[upper].p;
            const float a = length(b[fore].p - shoulder), c = length(b[hand].p - b[fore].p) + 0.07f;
            // The elbow out to his side and down along his body (towards his hips, a little towards the ground): lying
            // there it rests by his side, and a hand to his face folds up in front of him instead of sweeping round.
            const V3 pole = chest[2] * (s == 0 ? 0.8f : -0.8f) - chest[0] * 0.8f - world_up * 0.3f;
            const V3 elbow_at = reach_middle(shoulder, aim, a, c, pole);
            const float d = std::clamp(length(aim - shoulder), std::abs(a - c) + 0.02f, (a + c) * 0.98f);
            const float bend = pi - std::acos(std::clamp((a * a + c * c - d * d) / (2 * a * c), -1.0f, 1.0f));
            const V3 ux = normalized(elbow_at - shoulder, normalized(aim - shoulder, -chest[0]));
            V3 uy = across(aim - elbow_at, ux);
            if (length(uy) < 1e-3f) uy = across(b[upper].r[1], ux);
            uy = normalized(uy, b[upper].r[1]);
            // (Its z as the body's own is, whichever way round the game's axes are.)
            const float handed = dot(cross(b[upper].r[0], b[upper].r[1]), b[upper].r[2]) < 0 ? -1.0f : 1.0f;
            const M3 want{ux, uy, cross(ux, uy) * handed};
            // Quick, as a reach to one's own body is (peak 1.5-2 m/s at the hand); on the spot, it holds and presses.
            const Pace shoulder_pace{7.0f * quick, 35.0f * reaction, on ? 0.3f : 1.0f, on ? 1.5f * deg : 3 * deg};
            const Pace elbow_pace{9.0f * quick, 45.0f * reaction, on ? 0.3f : 1.0f, on ? 1.5f * deg : 3 * deg};
            move_to(upper, want, shoulder_pace, 0.5f, u, 1.0f);
            bend_to(fore, bend, elbow_pace, 0.5f, 0.85f * u, 1.0f);
            drive_of(hand).u = std::max(drive_of(hand).u, 0.1f);
        };
        // His belly, a little to one side (`side`: +1 his left), on its surface: where a hurt arm's hand is held in.
        const auto belly = [&](int side) {
            return Spot{centre(back1) + chest[1] * body::shapes[back1].radius + chest[2] * (0.05f * static_cast<float>(side)), chest[1]};
        };
        // A leg drawn up off the ground towards his chest: the thigh towards `dir` (the pelvis's axes), the knee bent to
        // `bend`, `amount` of the way from how it lay (the hugged knee, the racked curl), holding up its weight.
        const auto leg_up = [&](int s, V3 dir, float bend, float amount, float u) {
            if (amount < 0.01f) return;
            const auto i = static_cast<std::size_t>(s);
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin;
            lifted[i] = leg_busy[i] = true;
            const V3 from = normalized(base_[index_of()[thigh]][0], V3{-1, 0, 0});
            const float apart = std::acos(std::clamp(dot(from, normalized(dir)), -1.0f, 1.0f));
            const V3 goal = turn_towards(from, normalized(dir), apart * std::clamp(amount, 0.0f, 1.0f));
            const Pace pace{4.5f * quick, 25, 0.9f, 4 * deg};
            move_to(thigh, swing_to(thigh, turn_by(frame_of(thigh), goal)), pace, 0.45f, u, 1.0f);
            const float knee_from = bend_of(base_[index_of()[shin]]);
            bend_to(shin, lerp(knee_from, std::max(knee_from, bend), std::clamp(amount, 0.0f, 1.0f)), Pace{6.0f * quick, 35, 0.9f, 4 * deg}, 0.45f,
                    0.8f * u, 1.0f);
            out.leg_lifted[i] = true;
        };
        // A knee drawn up from how it lay with its foot staying on the ground (a leg whose foot is off the ground is left
        // as it lies), and never straightened against the ground (no crawling).
        const auto knee_up = [&](int s, float hip, float knee, float u) {
            const auto i = static_cast<std::size_t>(s);
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin, foot = s ? r_foot : l_foot, toe = s ? r_toe : l_toe;
            if ((!b[foot].touching && !b[toe].touching) || hip + knee < 0.01f) return;
            leg_busy[i] = true;
            const float more = 1.0f + asym_ * (s == 0 ? 1.0f : -1.0f);
            const M3 want = multiply(frame_of(thigh), multiply(rotation(V3{0, 0, -hip * more}), base_[index_of()[thigh]])); // -z: forwards
            const Pace pace{2.5f * quick, 15, 0.8f, 4 * deg};
            move_to(thigh, want, pace, 0.35f, u);
            drive_of(thigh).no_push = b[pelvis].r[2];
            const float knee_from = bend_of(base_[index_of()[shin]]);
            bend_to(shin, std::max(knee_from, std::min(knee_from + knee * more, 100 * deg)), pace, 0.35f, u);
            drive_of(shin).no_push = -tree.axis[shin];
            no_crawl[thigh] = no_crawl[shin] = true;
        };
        // The back curled from how he lay: forwards (`flex`), to his left (`side`), turned to his left (`twist`), spread
        // over the upper back's three joints (the low back's would tip the pelvis and swing the legs up off the ground);
        // heavily (it is the whole of him): no faster than 2 rad/s a joint, sped up at a third of an arm's rate.
        const auto curl_back = [&](float flex, float side, float twist, float u) {
            if (std::abs(flex) + std::abs(side) + std::abs(twist) < 0.01f) return;
            const float was = voluntary;
            voluntary = 10.0f;
            for (const Part p : {back1, chest2, chest3}) {
                const M3 want = multiply(frame_of(p), multiply(rotation(V3{twist / 3, -side / 3, flex / 3}), base_[index_of()[p]]));
                move_to(p, want, Pace{2.0f, 12, 0.6f, 2 * deg}, 0.35f, u);
            }
            voluntary = was;
        };
        const auto neck_bend = [&](float flex, float turn, float u) {
            if (std::abs(flex) + std::abs(turn) < 0.01f) return;
            for (const Part p : {neck, head}) {
                const M3 want =
                    multiply(frame_of(p), multiply(rotation(V3{turn * 0.5f, 0, flex * (p == neck ? 0.65f : 0.35f)}), base_[index_of()[p]]));
                move_to(p, want, Pace{3.0f, 20, 0.6f, 2 * deg}, 0.4f, u);
            }
        };
        const int hurt_arm = hurt_side > 0 ? 0 : 1, other_arm = 1 - hurt_arm;
        switch (hurt_) {
        case Hurt::head: {
            // Both hands to his head (his face, or the back of it if that is where it hit; lying on his face, its sides),
            // the knees drawn up a little, curled.
            const bool behind = hurt_local_known_ && hurt_local_[1] < 0.0f; // its centre is 2.5 cm in front of its joint
            const V3 face = b[head].r[1];
            const bool face_down = dot(face, world_up) < -0.3f;
            for (int s = 0; s < 2; ++s) {
                const float side = s == 0 ? 1.0f : -1.0f;
                const V3 out_n = face_down ? b[head].r[2] * side : normalized(face * (behind ? -1.0f : 1.0f) + chest[2] * (0.5f * side));
                hand_to(s, Spot{centre(head) + out_n * 0.1f, out_n}, 0.45f);
            }
            neck_bend(25 * deg * curl, 0, 0.3f);
            curl_back(0.5f * curl, 0, 0, 0.2f);
            for (int s = 0; s < 2; ++s) knee_up(s, 0.6f * curl, 1.0f * curl, 0.2f);
            break;
        }
        case Hurt::left_arm:
        case Hurt::right_arm: {
            // The hurt arm held in, its forearm across his belly, and the other hand cupping the elbow (the shoulder, for
            // a collarbone); he curls a little towards it, looking at it, knees up a little.
            const int s = hurt_arm;
            const Part fore = s ? r_forearm : l_forearm, upper = s ? r_arm : l_arm;
            const bool shoulder = hurt_part_ == l_collar || hurt_part_ == r_collar;
            hand_delay_[static_cast<std::size_t>(s)] = 0;
            hand_delay_[static_cast<std::size_t>(other_arm)] = std::min(hand_delay_[static_cast<std::size_t>(other_arm)], 0.1f);
            hand_to(s, belly(0), 0.35f);
            const Spot spot = shoulder ? surface_spot(upper, 0.0f, 0.15f, -hurt_side)
                              : hurt_part_ == upper ? surface_spot(upper, 0.35f, 1.0f, -hurt_side)
                                                    : surface_spot(fore, 0.0f, 0.6f, -hurt_side);
            hand_to(other_arm, spot, 0.45f, shoulder ? V3{} : b[hurt_part_ == upper ? upper : fore].r[0]);
            curl_back(0.35f * curl, 0.17f * curl * static_cast<float>(hurt_side), 0, 0.2f);
            neck_bend(0.4f * curl, 0.3f * curl * static_cast<float>(hurt_side), 0.25f);
            for (int k = 0; k < 2; ++k) knee_up(k, 0.4f * curl, 0.7f * curl, 0.2f);
            break;
        }
        case Hurt::left_hand:
        case Hurt::right_hand: {
            // The hurt hand pulled in to his belly, and the other hand round the hurt wrist from above (cradling it);
            // curled over it, knees up.
            const int s = hurt_arm;
            const Part fore = s ? r_forearm : l_forearm;
            hand_delay_[static_cast<std::size_t>(s)] = 0;
            hand_delay_[static_cast<std::size_t>(other_arm)] = std::max(hand_delay_[static_cast<std::size_t>(other_arm)], 0.3f);
            hand_to(s, belly(-hurt_side), 0.35f);
            hand_to(other_arm, surface_spot(fore, 0.7f, 0.95f, -hurt_side), 0.4f);
            curl_back(0.5f * curl, 0, 0, 0.2f);
            for (int k = 0; k < 2; ++k) knee_up(k, 0.5f * curl, 0.8f * curl, 0.2f);
            break;
        }
        case Hurt::left_leg:
        case Hurt::right_leg: {
            // That knee drawn up to his chest (the one time a leg comes off the ground lying down) and both hands on it
            // (the knee, or the thigh where it was hit), rubbing it; the other knee drawn up a little, its foot down.
            const int s = hurt_side > 0 ? 0 : 1;
            const float out_z = s == 0 ? 1.0f : -1.0f;
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin;
            leg_up(s, V3{0.42f, 0.9f, 0.05f * out_z}, 120 * deg, curl, 0.4f);
            const bool on_thigh = hurt_part_ == thigh;
            const Spot spot = on_thigh ? surface_spot(thigh, 0.3f, 0.85f, hurt_side) : surface_spot(shin, 0.02f, 0.5f, hurt_side);
            for (int k = 0; k < 2; ++k) {
                Spot side = spot;
                side.at = side.at + chest[2] * (k == 0 ? 0.04f : -0.04f);
                hand_to(k, side, 0.45f, b[on_thigh ? thigh : shin].r[0]);
            }
            knee_up(1 - s, 0.5f * curl, 0.8f * curl, 0.2f);
            curl_back(0.4f * curl, 0, 0, 0.2f);
            neck_bend(0.4f * curl, 0, 0.25f);
            break;
        }
        case Hurt::left_hip:
        case Hurt::right_hip:
        case Hurt::back: {
            // The same side's hand pressed flat on the spot (a hip rubbed), knees up, and a quarter turn off it (the knees
            // over to the other side, the back turning with them): at most onto his side.
            const int side = hurt_side != 0 ? hurt_side : (roll_side_ > 0 ? -1 : 1);
            const int s = side > 0 ? 0 : 1;
            hand_delay_[static_cast<std::size_t>(s)] = 0;
            hand_to(s, surface_spot(hurt_part_, 0.1f, 0.9f, side), 0.4f, hurt_ == Hurt::back ? V3{} : chest[0]);
            for (int k = 0; k < 2; ++k) knee_up(k, 0.6f * curl, 1.0f * curl, 0.2f);
            curl_back(0.25f * curl, 0, -0.25f * curl * static_cast<float>(side), 0.2f);
            break;
        }
        case Hurt::left_ribs:
        case Hurt::right_ribs: {
            // That side's hand (the forearm across him) pressed to the ribs, curled and leaning over them, still.
            const int s = hurt_arm;
            hand_delay_[static_cast<std::size_t>(s)] = 0;
            hand_to(s, surface_spot(hurt_part_, 0.1f, 0.9f, hurt_side), 0.4f);
            curl_back(0.6f * curl, 0.25f * curl * static_cast<float>(hurt_side), 0, 0.22f);
            neck_bend(0.3f * curl, 0, 0.2f);
            break;
        }
        case Hurt::groin: {
            // Racked, the worst of all, and the one hurt people roll about with: both hands between his thighs at once,
            // clamped there (squeezing harder every few seconds); the thighs together and drawn up, then all the way into a
            // ball (knees to the chest, back rounded, head down) by two seconds; then rocking from side to side.
            const V3 groin = centre(pelvis) - b[pelvis].r[0] * 0.12f + b[pelvis].r[1] * 0.07f;
            const float squeeze = 0.2f * std::pow(std::max(0.0f, std::sin(2 * pi * t / 3.1f + rock_phase_)), 4.0f);
            hand_delay_ = {0.0f, 0.08f};
            for (int s = 0; s < 2; ++s) hand_to(s, Spot{groin + b[pelvis].r[2] * (s == 0 ? 0.03f : -0.03f), b[pelvis].r[1]}, 0.7f + squeeze);
            // Still astride the rail (nothing but the pelvis and legs on anything, the pelvis high), the thighs only squeeze
            // it and fold a little: drawn right up there, he would pitch off it head first. The ball comes on the ground.
            const bool lying = b[spine].touching || b[back1].touching || b[chest2].touching || b[chest3].touching || b[head].touching ||
                               centre(pelvis)[1] - ground < 0.3f;
            const float full = lying ? smooth((since - 0.35f) / 1.2f) : 0.0f;
            const float rocking = rhythm && lying ? smooth((since - 1.8f) / 0.8f) : 0.0f;
            const float roll = rocking * (0.25f * std::sin(2 * pi * t / 2.4f + rock_phase_) + 0.1f * std::sin(2 * pi * t / 1.5f + 2 * rock_phase_)) +
                               0.2f * rocking * static_cast<float>(roll_side_);
            for (int s = 0; s < 2; ++s) {
                const float out_z = s == 0 ? 1.0f : -1.0f;
                const float hip_deg = (lying ? lerp(75, 115, full) : 35.0f) * deg;
                const V3 dir{-std::cos(hip_deg), std::sin(hip_deg), -0.12f * out_z + 0.5f * roll};
                leg_up(s, dir, (lying ? lerp(75, 130, full) : 60.0f) * deg, lying ? curl : 0.4f * curl, 0.45f);
            }
            curl_back(lerp(0.35f, 0.9f, full) * curl, 0, 0.4f * roll * curl, 0.3f);
            neck_bend(lerp(0.2f, 0.6f, full) * curl, 0.5f * roll, 0.3f);
            break;
        }
        case Hurt::none: break;
        }
    }
    // In the reaction, the legs' joints never push the leg straight against the ground.
    for (std::size_t k = 0; k < joint_count; ++k)
        if (!no_crawl[ranges()[k].child]) drive[k].no_push = {};

    // ---- nothing left up in the air --------------------------------------------------------------------------------
    // Once he is down, an arm or a leg doing nothing (not reaching, holding or catching) is not left up in the air: lying
    // there with a limb held up against gravity reads as knocked out (the fencing response), and the game holds a limb
    // where it stops. While it still moves, it is turned down onto the ground or his body, with no more than a quarter of
    // the arm's strength (it is let fall, not posed).
    easing = 1;
    if (fell && phase_ != Phase::brace && (torso_off_for_ < 0.25 || down_)) {
        const auto resting_on_him = [&](const V3& at) {
            for (const Part p : {pelvis, spine, back1, chest2, chest3, head, l_thigh, r_thigh})
                if (length(at - centre(p)) < body::shapes[p].radius + 0.12f) return true;
            return false;
        };
        for (int s = 0; s < 2; ++s) {
            const auto i = static_cast<std::size_t>(s);
            const Part upper = s ? r_arm : l_arm, fore = s ? r_forearm : l_forearm, hand = s ? r_hand : l_hand;
            if (arm_busy[i]) {
                arm_down_at_[i] = -1;
                continue;
            }
            const bool held_up = !b[hand].touching && !b[fore].touching && !b[upper].touching &&
                                 (centre(hand)[1] - ground > 0.22f || centre(fore)[1] - ground > 0.25f) && !resting_on_him(centre(hand));
            if (!held_up) {
                arm_down_at_[i] = -1;
                continue;
            }
            if (arm_down_at_[i] < 0) arm_down_at_[i] = t_;
            if (reach_[i] == Reach::none && !pain) reach_[i] = Reach::resting;
            // Down along the ground at his side: the upper arm kept pointing where it points across the ground, tipped
            // down to it (straight up: out to his side); the forearm follows as it falls (straightening it would lift the
            // hand).
            V3 across_ground = flat(b[upper].r[0]);
            if (length(across_ground) < 0.03f) across_ground = flat(chest[2] * (s == 0 ? 1.0f : -1.0f) + chest[0] * -0.5f);
            const V3 dir = normalized(normalized(across_ground) * 0.8f - world_up * 0.6f);
            // (A swing of the bone only, its twist left as it is: the stops keep it in range.)
            move(upper, seek(b[upper].r, swing_to(upper, dir), Pace{4.0f, 25, 1.0f, 6 * deg}), 0.4f, 0.25f);
        }
        for (int s = 0; s < 2; ++s) {
            const auto i = static_cast<std::size_t>(s);
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin, foot = s ? r_foot : l_foot, toe = s ? r_toe : l_toe;
            if (leg_busy[i] || torso_off_for_ > 0.25) {
                leg_down_at_[i] = -1;
                continue;
            }
            const bool held_up = !b[foot].touching && !b[toe].touching && !b[shin].touching && centre(foot)[1] - ground > 0.25f &&
                                 !resting_on_him(centre(foot));
            if (!held_up) {
                leg_down_at_[i] = -1;
                continue;
            }
            if (leg_down_at_[i] < 0) leg_down_at_[i] = t_;
            // (The way it already leans, however little: tipped the other way it would go up over the top first.)
            V3 across_ground = flat(b[thigh].r[0]);
            if (length(across_ground) < 0.03f) across_ground = flat(-b[pelvis].r[0]);
            const V3 dir = normalized(normalized(across_ground) * 0.85f - world_up * 0.5f);
            move(thigh, seek(b[thigh].r, swing_to(thigh, dir), Pace{3.0f, 20, 0.8f, 6 * deg}), 0.35f, 0.15f);
        }
    }
    voluntary = 0;
    easing = 1;
    for (int s = 0; s < 2; ++s) out.reach[static_cast<std::size_t>(s)] = static_cast<std::uint8_t>(reach_[static_cast<std::size_t>(s)]);

    // ---- the muscles ---------------------------------------------------------------------------------------------
    // Each joint's activation follows what it is asked (40 ms to tense, 100 ms to let go). Each joint's push towards its
    // turning is set against its inertia as the free linked bodies present it, so a share means what it says whatever
    // hangs off the joint, and the whole body's are solved together in one step (Tan, Liu and Turk 2011's stable PD,
    // taken at the end of the step), the linked bodies moving as Featherstone's method has them (real_bails_body.h). A
    // joint whose torque comes out over what it has (activation x its strength), or that would push a way it must not, is
    // held to what it may and the rest solved again.
    const std::array<M3, part_count> apparent = tree.respond();
    // A hand, foot or toes on the ground is the ground's: it is not written, and its own joint is left to the ground and
    // the game. Not the kicking foot, a hand on its way to what hurts, nor a foot drawn up with its leg.
    std::array<bool, part_count> planted{}, written_body{};
    const Part kick_foot = kick_leg_ > 0 ? r_foot : l_foot, kick_toe = kick_leg_ > 0 ? r_toe : l_toe;
    for (std::size_t i = 1; i < part_count; ++i) {
        const auto p = static_cast<Part>(i);
        const bool lifting = (lifted[0] && (p == l_foot || p == l_toe)) || (lifted[1] && (p == r_foot || p == r_toe)) ||
                             (reaching[0] && p == l_hand) || (reaching[1] && p == r_hand);
        planted[i] = is_end(p) && b[i].touching && !(kicking && (p == kick_foot || p == kick_toe)) && !lifting;
        written_body[i] = !planted[i];
    }
    // A hurt arm is weaker for the rest of the bail (down to 40% of its strength as its hurt grows); and every joint is
    // weaker for a moment after a hit (Euphoria's weakness at a hit: back over 0.4 s).
    std::array<float, part_count> weak{};
    weak.fill(1.0f);
    for (int s = 0; s < 2; ++s) {
        const float hurt = std::max(damage_[static_cast<std::size_t>(s ? Hurt::right_arm : Hurt::left_arm)],
                                    damage_[static_cast<std::size_t>(s ? Hurt::right_hand : Hurt::left_hand)]);
        const float keep = 1.0f - 0.6f * std::clamp(hurt / 3.0f, 0.0f, 1.0f);
        for (const Part p : s ? std::array<Part, 3>{r_arm, r_forearm, r_hand} : std::array<Part, 3>{l_arm, l_forearm, l_hand}) weak[p] = keep;
    }
    const float after_hit = 1.0f - weak_depth_ * (1.0f - smooth(static_cast<float>(t_ - weak_at_) / 0.4f));
    std::array<float, part_count> weak_tone = weak;
    for (auto& w : weak_tone) w *= after_hit;
    std::array<V3, part_count> push{}, held{}, no_push{};
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
        // (The weakness at a hit is the tone's and the reflexes': a move he makes comes after it, at his strength.)
        cap[c] = std::max(act_[k], 0.02f) * body::shapes[c].torque * strength * (d.goal ? weak[c] : weak_tone[c]);
        no_push[c] = d.no_push;
        if (body::weld(c) || planted[c]) continue;
        const V3 rate = turning(c);
        // Closing `share` of the gap in a step: an armature of share / (1 - share) of the joint's inertia.
        const float s = std::clamp(d.share, 0.0f, 0.85f);
        const float beta = s / (1.0f - s);
        armature[c] = scaled(apparent[c], beta);
        // A move speeds a joint up or turns it round no faster than a person's muscles do (a reflex's shoulder about 80
        // rad/s^2; legs and the back, heavier, less): a change of way all in one step is a flick.
        // A new move builds up over 0.15 s (a muscle's activation rises in about 40 ms, a reach's speed peaks 0.2 s in),
        // so it never sets off with a jolt.
        V3 gap = d.target - rate;
        if (d.goal && !was_goal_[c]) goal_since_[c] = t_;
        was_goal_[c] = d.goal;
        float carry_on = 1;
        if (d.goal) {
            const Group g = group_of(c);
            const float reflex = g == arm_left || g == arm_right ? 80.0f * reaction : g == leg_left || g == leg_right ? 50.0f : 40.0f;
            const float since = static_cast<float>(t_ - goal_since_[c]);
            const float onset = std::max(smooth(since / 0.15f), 0.05f);
            const float most = (d.accel > 0 ? d.accel : reflex) * onset * dt;
            if (s * length(gap) > most) gap = gap * (most / (s * length(gap)));
            carry_on = smooth(since / 0.15f);
        }
        // What it holds up of the weight hanging off it: the torque that holds that up (one side of the joint against
        // the other, as every muscle torque here), taken on as the move sets off.
        const V3 lift = tree.hold[c] * (std::clamp(d.carry, 0.0f, 1.0f) * carry_on * dt);
        if (body::hinge(c)) {
            const V3 n = tree.axis[c];
            push[c] = n * (apparent[c][0][0] * beta * dot(gap, n) + dot(lift, n));
        } else {
            push[c] = turn_by(apparent[c], gap * beta) + lift;
        }
        if (!finite(push[c]) || !finite(armature[c])) {
            push[c] = {};
            armature[c] = {};
        }
    }
    float mean = 0;
    for (const float a : act_) mean += a;
    out.tone = mean / static_cast<float>(joint_count);
    // The board pushing back on the foot that kicks it, when the board gets the kick (the one push from outside).
    std::array<V3, part_count> external{};
    bool pushed_back = false;
    V3 want_linear{}, want_angular{};
    if (kick_leg_ != 0 && !kick_shot_ && t_ - kick_at_ >= 0.05) {
        kick_shot_ = true;
        out.board_kick = true;
        out.board_dv = board_dv_;
        if (in.board.writable && length(board_dv_) > 0) {
            const V3 back = -board_dv_ * board_mass;
            external[kick_foot] = back;
            want_linear = back;
            want_angular = cross(mass.at[kick_foot], back);
            pushed_back = true;
        }
    }
    const auto* outside = pushed_back ? &external : nullptr;
    // The torque a joint ends up with, from the solve: what it pushed less what its own armature took back as it moved.
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
        tree.solve(impulse, dw, dv, outside);
        bool changed = false;
        for (std::size_t k = 1; k < body::chain.size() && round < 4; ++k) {
            const Part c = body::chain[k];
            if (saturated[c]) continue;
            V3 torque = torque_of(c);
            bool over = length(torque) > cap[c] * 1.0001f;
            const float wrong_way = dot(torque, no_push[c]);
            if (wrong_way > 0.5f) {
                torque = torque - no_push[c] * wrong_way;
                over = true;
            }
            if (over) {
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
    // In the game: each joint's torque is handed over scaled by one over the share the game keeps at that joint's speed
    // now (gain_for), so what stays is that torque; never so much that the joint's turning changes by more than the game
    // could ever let it (most_change); and at half for a while after the joint overshot. Still one side of each joint
    // against the other through the linked bodies: the body's momentum is kept. (`torque` and `work` stay what the muscles
    // mean.)
    if (in.in_game) {
        std::array<V3, part_count> handed{};
        float gains = 0;
        int counted = 0;
        for (std::size_t k = 1; k < body::chain.size(); ++k) {
            const Part c = body::chain[k];
            if (body::weld(c) || planted[c]) continue;
            const V3 rate = turning(c);
            const float speed = length(rate);
            // Overshot: what it was pushed towards came the other way round, more than half as large as before.
            const Drive& d = drive_of(c);
            const V3 err = d.target - rate;
            if (d.goal && length(error_[c]) > 0.3f && dot(err, error_[c]) < 0 && length(err) > 0.5f * length(error_[c])) overshot_at_[c] = t_;
            error_[c] = d.goal ? err : V3{};
            const float g = gain_for(c, speed) * (t_ - overshot_at_[c] < 0.3 ? 0.5f : 1.0f);
            handed[c] = torque_of(c) * (dt * g);
            // How far that alone would change the joint's turning (its own inertia), against the most allowed.
            const float change = body::hinge(c) ? std::abs(dot(handed[c], tree.axis[c])) / std::max(apparent[c][0][0], 1e-6f)
                                                : length(turn_by(inverse(apparent[c]), handed[c]));
            const float most = most_change(speed);
            if (change > most && std::isfinite(change)) handed[c] = handed[c] * (most / change);
            if (length(handed[c]) > 1e-6f) {
                gains += g;
                ++counted;
            }
        }
        out.gain = counted ? gains / static_cast<float>(counted) : 1.0f;
        tree.articulate(std::array<M3, part_count>{});
        tree.solve(handed, dw, dv, outside);
    }

    // ---- the joints' stops -----------------------------------------------------------------------------------------
    // Every joint, every step: its speed into a stop (with what the muscles add) is cut so it comes to rest on it, and
    // past it it goes no further. Stops are bone and ligament, not muscle: no strength caps them. Each is an impulse at
    // its joint through the linked bodies, one joint after the other from the pelvis out, twice over.
    std::array<M3, part_count> stiff{};
    bool stiff_known = false;
    for (int round = 0; round < 2; ++round) {
        bool any = false;
        for (const auto& l : ranges()) {
            const Part c = l.child;
            if (body::weld(c) || planted[c]) continue;
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
    keep_momentum(mass, written_body, dw, dv, want_linear, want_angular);
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
