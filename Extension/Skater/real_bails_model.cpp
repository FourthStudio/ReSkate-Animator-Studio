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
// In the game a muscle's push does not all stay: the game's own joint solve takes part of each step's change back. So in
// the game each joint's torque is handed over larger by about what is lost (gain x what stays = 1), and what reaches the
// body is what the muscles mean. Measured from the user's recordings of 2026-11-02 (21 bails, rb_track.py: each joint's
// commanded turn against the next step's): of each written change the game kept 24% at a shoulder, 27% at an elbow, 57%
// at a hip, 43% at a knee, 51% along the back, 99% at the neck and head, and 34% at a wrist, an ankle or the toes. The
// second set (25 bails with these gains, the fall only: brace and impact, rb_track_phase.py) kept 19% at a shoulder, 17%
// at an elbow, 73% at a hip, 45% at a knee, 37% along the back, 54% at the neck and 26% at a wrist or ankle: these are
// set from it. (Lying down it kept almost nothing, but that was the planted hands: see `reaching` below.)
float game_gain(Part c) noexcept {
    switch (c) {
    case l_arm: case r_arm: return 5.0f;
    case l_forearm: case r_forearm: return 5.5f;
    case l_thigh: case r_thigh: return 1.4f;
    case l_shin: case r_shin: return 2.2f;
    case spine: case back1: case chest2: case chest3: return 2.6f;
    case l_hand: case r_hand: case l_foot: case r_foot: case l_toe: case r_toe: return 3.6f;
    case neck: case head: return 1.8f;
    default: return 1.0f; // the collarbones (welded: nothing of their own)
    }
}
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
// or a goal the joint is turned towards (a reach, the chin, the kick, what hurts) with a stiffness and damping ratio.
struct Drive {
    float u{};            // activation, 0..1: its torque is at most u x its strength
    float damp{1};        // tone: how fast it takes the joint's own turning out, per second
    bool goal{};
    M3 want{identity3()}; // the goal: the child's axes in the joint's frame (a hinge: only its bend counts)
    float wn{3}, zeta{1};
    V3 spin{};            // a reflex: the joint's own turning (world) its damping pulls towards, instead of none
    V3 no_push{};         // a way (world, unit) the joint must not push: a leg whose foot is on the ground, hurt, does
                          // not straighten (that would push him along the ground: a crawl)
    float carry{};        // how much of the weight of what hangs off the joint its muscles carry, 0..1 (a hand going to
                          // what hurts, a knee drawn up: lifted, not dragged up by a spring that gravity holds short)
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
    // The angular momentum wanted about com, as about the written bodies' centre: less that centre's share of the push.
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
// The stops: inside 15 degrees of one a joint's speed into it is cut so it comes to rest on it in 80 ms; past it (the
// game's own range is a little wider, and a slam can push a joint there) it goes no further, but it is not pushed back:
// a stop that pushes back is a motor, and one that springs back adds energy.
constexpr float stop_zone = 15 * deg, stop_brake = 0.08f, stop_back = 0.0f;
// A complete skateboard: 2 to 3.4 kg, most 2.3 to 2.8.
constexpr float board_mass = 2.5f;
// The kick: 100 to 200 ms, the board leaving him at 2 to 4 m/s.
constexpr float kick_time = 0.16f, kick_speed = 3.0f;
// A minimum-jerk path from 0 to 1: how a person's hand moves from one place to another (slow off, quickest halfway,
// slow in), so a move never starts or stops with a jolt.
float min_jerk(float x) noexcept {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * x * (10 + x * (-15 + 6 * x));
}
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
    // racked he rolls over one way or the other.
    kick_onset_ = 0.17f + 0.13f * std::abs(draw());
    turn_side_ = draw() > 0 ? 1 : -1;
    roll_side_ = draw() > 0 ? 1 : -1;
    rock_phase_ = pi * draw();
    const auto& b = in.body;
    for (auto& r : rest_) r = identity3();
    for (std::size_t i = 1; i < part_count; ++i)
        if (b[i].ok && b[parent_of[i]].ok && finite(b[i].r) && finite(b[parent_of[i]].r)) rest_[i] = multiply(transposed(b[parent_of[i]].r), b[i].r);
    for (auto& m : base_) m = identity3();
    landed_at_.fill(-10);
    limb_hit_at_.fill(-10);
    region_at_.fill(-10);
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

    // ---- what the game keeps -------------------------------------------------------------------------------------------
    // In the game, how much of last step's push each joint kept: its turn from before we wrote to now, against what we
    // asked, in running sums over about a quarter of a second. The game's share is nothing like one number: in the user's
    // recordings it kept 17-73% of a turn in the air (by joint), 5-11% of one on a limb still moving on the ground, and none
    // on a limb lying still. A fixed gain was too strong in the air or did nothing on the ground; this one follows it.
    if (in.in_game && asked_known_) {
        const float fade = std::exp(-dt / 0.25f);
        for (const auto& l : ranges()) {
            const Part c = l.child;
            kept_got_[c] *= fade;
            kept_asked_[c] *= fade;
            const float asked2 = dot(asked_[c], asked_[c]);
            if (asked2 < 1e-4f || !finite(asked_[c])) continue;
            const V3 got = (b[c].w - b[l.frame].w) - before_[c];
            if (!finite(got)) continue;
            kept_got_[c] += dot(got, asked_[c]);
            kept_asked_[c] += asked2;
        }
    }
    asked_known_ = false;
    // What to hand the game for a joint so what stays is what the muscles mean: one over the share it keeps, starting
    // from the measured average for that joint (game_gain) until it has been asked something, at most 10 times (a limb
    // lying still keeps nothing: more would only flick it once it moves).
    const auto gain_of = [&](Part c) {
        const float prior = 0.5f; // (rad/s)^2 of asking at the average share
        const float kept = (kept_got_[c] + prior / game_gain(c)) / (kept_asked_[c] + prior);
        return std::clamp(1.0f / std::max(kept, 0.1f), 1.0f, 10.0f);
    };

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
    // Which way he goes over: once that is clear (he leans or it is over half a metre a second, or he is about to land) it
    // is kept, and then turns no faster than 4 rad/s, so the arms do not swap sides on a wobble. Until then the way his
    // momentum takes him now, or, with next to none, the way he faces.
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
    // flinch at most, nothing to hold. Each
    // hit adds how much more than 1.5 m/s it was, times its place's share (site_factor); a hand that came down to catch
    // the fall counts under half (it meant to land: a wrist hurts most only when nothing else hit hard); racked counts
    // most of all, however soft. Where on the body each place was last hit is kept (the hand goes to that spot).
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
            // A hand that came down to catch, or its forearm or upper arm coming down after it within a third of a second
            // (the catch rolling on up the arm), meant to land: it counts under half. In the recordings the arm that
            // caught was nearly always what hurt most, so he held an arm after almost every bail.
            // (A hand that came down during the fall, not one on the ground from the start.)
            const auto caught_just = [&](int s) {
                const double at = caught_at_[static_cast<std::size_t>(s)];
                return at >= 0.05 && t_ - at < 0.35;
            };
            const bool catching = (p == l_hand && caught_at_[0] >= 0) || (p == r_hand && caught_at_[1] >= 0) ||
                                  ((p == l_forearm || p == l_arm) && caught_just(0)) || ((p == r_forearm || p == r_arm) && caught_just(1));
            add(h, p, speed, (speed - 1.5f) * site_factor(h) * (catching ? 0.45f : 1.0f));
        }
        // Racked hurts a lot however soft (at least as much as a hard knock on the head), and most of all.
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
            // He takes in what happened (170 to 300 ms) before he reaches for it; racked, at once; a hard knock on the
            // head leaves him stunned a moment longer first.
            hurt_delay_ = h == Hurt::groin ? 0.1f : 0.2f + 0.1f * std::abs(draw());
            if (h == Hurt::head && hurt_speed_ > 5.0f) hurt_delay_ += 0.8f;
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
    const bool hurt_on = in.tune.grab_hurt && hurt_ != Hurt::none;
    const bool racked_now = hurt_ == Hurt::groin;
    // How long he holds it: about Hold what hurts for, more for a harder hit and for what hurts more (racked longest).
    // How bad it is: what that place has taken, against one hit of 5.5 m/s there.
    const float severity = hurt_ == Hurt::none ? 0.0f
                                               : std::clamp(damage_[static_cast<std::size_t>(hurt_)] / (4.0f * site_factor(hurt_)), 0.0f, 1.0f);
    const float hold_for = std::clamp(hold_hurt / 4.5f * hold_base(hurt_) * (0.5f + (racked_now ? 1.0f : severity)), 1.0f, 15.0f);
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

    // Down: on something with his trunk (the ground, or the rail he is astride), not tumbling, flying or sliding fast, and
    // no hard landing coming in the next 0.3 s. Slow enough under 3.5 m/s and 4.5 rad/s of the trunk, and fast again over
    // 4.5 m/s or 6 rad/s: he reacts while he still slides. It has to start then: in the user's recordings the game kept
    // none of a push on a limb lying still on the ground (and 5-11% while it still moved), so a reaction started once he
    // was still only twitched; started while he slides it gets there, and the pose stays as he comes to rest. Racked he
    // holds on even falling off the rail.
    {
        const float speed = length(mass.velocity);
        slow_ = slow_ ? speed < 4.5f && torso_spin < 6.0f : speed < 3.5f && torso_spin < 4.5f;
        const bool landing_soon = pred.hit && pred.t < 0.3f && out.hit_speed > 2.5f;
        down_ = racked_now ? slow_ : slow_ && torso_off_for_ < 0.25 && !airborne && !landing_soon;
        down_for_ = down_ ? down_for_ + dt : 0;
    }
    out.down = down_;
    // The reaction to what hurts: once he is down, a moment after the hit (hands on the spot 0.4 to 0.8 s after it), for
    // about as long as Hold what hurts for says; never while he tumbles, flies or slides fast (it stops at once, and starts
    // again from how he lies once he is slow again), nor going down again (the arms catch first), nor stunned by a slam.
    const bool pain = hurt_on && down_ && down_for_ >= 0.05 && t_ - hurt_at_ >= hurt_delay_ && held_ < hold_for &&
                      (racked_now || (phase_ != Phase::brace && t_ >= stun_until_));
    if (pain && !pain_) {
        for (std::size_t k = 0; k < joint_count; ++k) {
            const auto& l = ranges()[k];
            base_[k] = multiply(transposed(b[l.frame].r), b[l.child].r);
        }
        pain_since_ = t_;
        let_go_at_ = -1;
        gave_up_at_ = {-1, -1};
    }
    // Held for as long as it lasts and still down: he lets go slowly (the hands ease off, the curl eases out). Moving
    // fast again ends everything at once.
    if (pain_ && !pain && down_ && held_ >= hold_for) let_go_at_ = t_;
    const float let_go = racked_now ? 3.0f : hurt_ == Hurt::left_leg || hurt_ == Hurt::right_leg ? 2.5f : 1.5f;
    if (!down_ || (let_go_at_ >= 0 && t_ - let_go_at_ > let_go)) let_go_at_ = -1;
    pain_ = pain;
    if (pain) held_ += dt;
    out.pain = pain;

    // ---- what each joint is asked ----------------------------------------------------------------------------------
    // Tone, by group, for the phase. The neck's is Head protection's, and it never drops below 0.2 unless he is stunned
    // (a head that lolls reads as knocked out).
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
    for (std::size_t k = 0; k < joint_count; ++k) {
        auto& d = drive[k];
        const Part c = ranges()[k].child;
        d.u = tone[group_of(c)];
        // Tense muscle resists being turned fast: from about 1/s (lying there) to 3.5/s (falling, tense).
        d.damp = 1.0f + 12.0f * d.u * strength;
        if (thud[c] && heavy > 0.01f) {
            d.u = std::max(d.u, 0.25f * heavy);
            d.damp = std::max(d.damp, (4.0f + 4.0f * heavy) * strength);
        }
    }
    const auto drive_of = [&](Part c) -> Drive& { return drive[index_of()[c]]; };
    const auto rel_of = [&](Part c) {
        const auto& l = ranges()[index_of()[c]];
        return multiply(transposed(b[l.frame].r), b[c].r);
    };
    const auto frame_of = [&](Part c) -> const M3& { return b[ranges()[index_of()[c]].frame].r; };
    // A ball joint's goal: its bone swung to point along `dir` (the frame's axes), its twist left as it is.
    const auto swing_goal = [&](Part c, const V3& dir) {
        const M3 now = rel_of(c);
        return multiply(rotation(swing_between(normalized(now[0], dir), dir)), now);
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
            d.damp = std::max(d.damp, (3.0f + 9.0f * s * most) * strength);
            out.reflex[c] = true;
        }
        const bool may_pull = arm || ((phase_ == Phase::brace || phase_ == Phase::impact || phase_ == Phase::tumble) && !down_);
        if (pull > 0 && may_pull) {
            drive_of(bend).spin = tree.axis[bend] * ((2.0f + 4.0f * s) * pull);
            const V3 r = limb_hit_point_[k] - b[root].p;
            drive_of(root).spin = normalized(cross(r, limb_hit_from_[k]), V3{}) * ((1.5f + 3.0f * s) * pull);
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
                    d.damp = std::max(d.damp, (1.0f + 12.0f * d.u * strength) * (1.0f + 2.0f * e));
                    d.spin = frame_of(p)[2] * ((1.5f + 2.5f * s) * e * face_first * (p == neck ? 1.0f : 0.5f));
                    out.reflex[p] = true;
                }
            for (const Part p : {spine, back1, chest2, chest3}) {
                auto& d = drive_of(p);
                d.u = std::max(d.u, 0.2f * e);
                d.spin = d.spin + frame_of(p)[2] * ((0.3f + 0.4f * s) * e);
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
                // Face first: the head turned and the chin a little up, the more so the more squarely forwards.
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
            // Harder as a hit lands on him (x1.3 while the impact lasts), at most 0.9.
            const float hard = std::min(u * head_k * (phase_ == Phase::impact ? 1.3f : 1.0f), 0.9f);
            const float turn = turn_ * static_cast<float>(turn_side_);
            for (const Part p : {neck, head}) {
                auto& d = drive_of(p);
                const bool lower = p == neck;
                d.goal = true;
                d.want = rotation(V3{turn * (lower ? 0.4f : 0.6f), side_ * (lower ? 0.65f : 0.35f),
                                     chin_ * (lower ? 0.65f : 0.35f) - lift_ * (lower ? 0.6f : 0.4f)});
                d.wn = 8 * std::sqrt(std::min(head_k, 1.5f)); // its stiffness, as its strength, is Head protection's
                d.zeta = 1;
                d.u = std::max(d.u, hard);
            }
        }
    }

    // ---- the arms going down --------------------------------------------------------------------------------------
    // From the moment the bail starts, the arms reach the way his momentum takes him: forwards, back and down, or out to
    // the side he goes (blended), for where the ground will be when he gets there. The arm on the side he falls to leads,
    // 20 to 60 ms before the other, and reaches further. Short of time (under 0.45 s to the hit) they still go, but get
    // only part of the way: bent, nearer the chest (a startle). Tumbling through the air they frame the head; dropping
    // they go out to the sides, then forwards and down for the landing.
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
                auto& el = drive_of(fore);
                if (since_catch < 0.3) {
                    sh.u = std::max(sh.u, 0.45f);
                    sh.damp = std::max(sh.damp, 8.0f * strength);
                    el.goal = true;
                    el.want = rotation(V3{0, 0, lerp(caught_bend_[i], 100 * deg, min_jerk(static_cast<float>(since_catch) / 0.25f))});
                    el.wn = 6;
                    el.zeta = 1.6f;
                    el.u = std::max(el.u, 0.4f);
                } else {
                    sh.u = 0.06f;
                    el.u = 0.06f;
                    sh.damp = el.damp = 1.5f;
                }
                continue;
            }
        }
        const float onset = (arm_latency + arm_delay_[i] + arm_jitter_[i]) / reaction;
        const bool reach = in.tune.catch_arms && going_down && lead_ != 0 && t >= onset && !(pain && racked_now);
        if (reach) {
            // Little time before the hit: out quicker, and it arrives bent; with time it is long.
            const float urgency = std::clamp((0.7f - to_hit) / 0.5f, 0.0f, 1.0f);
            const float late = std::clamp((0.35f - to_hit) / 0.25f, 0.0f, 1.0f);
            // How far it gets to go: all the way with 0.45 s or more to the hit, half with under 0.25 s (it is set when the
            // arm sets off, and only grows if the fall turns out longer).
            amp_[i] = std::max(amp_[i], std::clamp((to_hit - 0.25f) / 0.2f, 0.5f, 1.0f));
            V3 dir{};
            float bend = (35 + 20 * late) * deg * (lead ? 0.75f : 1.0f), firm = 1.0f;
            if (kind_ == Fall::spin) {
                // Tumbling through the air: the forearms up in front of the head, framing it.
                dir = normalized(V3{0.42f, 0.85f, 0.3f * out_z});
                bend = 130 * deg;
                firm = 0.8f;
            } else if (kind_ == Fall::drop) {
                // Dropping: first out to the sides, then, for the landing, forwards and down.
                const float landing = std::clamp((0.4f - to_hit) / 0.15f, 0.0f, 1.0f);
                dir = normalized(V3{lerp(-0.17f, -0.6f, landing), lerp(0.05f, 0.6f, landing), lerp(0.98f, 0.45f, landing) * out_z});
                bend = lerp(45, 30, landing) * deg;
            } else {
                // Set in the world (down to the ground, ahead of him, back and out, or to the side he goes) and read in the
                // upper chest's axes as they will be when he lands, so it is where the ground will be for the arm, not where
                // it is now. Sideways, the arm on the fall's side goes out to it; the high arm forwards and across.
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
            // Short of time, part of the way: from the startle (the upper arm forwards and out, the elbow bent, hands
            // coming up in front of the chest) towards the reach.
            if (amp_[i] < 1.0f && kind_ != Fall::spin) {
                const V3 startle = normalized(V3{-0.75f, 0.55f, 0.35f * out_z});
                const float apart = std::acos(std::clamp(dot(startle, dir), -1.0f, 1.0f));
                dir = turn_towards(startle, dir, apart * amp_[i]);
                bend = lerp(60 * deg, bend, amp_[i]);
            }
            const V3 now = normalized(rel_of(upper)[0]);
            if (length(aim_[i]) < 0.5f) aim_[i] = now; // the aim starts where the arm is
            aim_[i] = turn_towards(normalized(aim_[i], now), dir, (6.0f + 6.0f * urgency) * reaction * smooth((t - onset) / 0.1f) * dt);
            auto& sh = drive_of(upper);
            sh.goal = true;
            sh.want = swing_goal(upper, aim_[i]);
            sh.wn = std::clamp(3.0f / std::max(to_hit - 0.08f, 0.05f), 6.0f, 13.0f);
            sh.zeta = 0.8f;
            sh.u = std::min((0.45f + 0.15f * urgency) * firm, 0.8f);
            auto& el = drive_of(fore);
            el.goal = true;
            el.want = rotation(V3{0, 0, bend});
            el.wn = 10;
            el.zeta = 0.8f;
            el.u = sh.u;
            // The elbow stiffens 100 to 130 ms before the hands land, ready to take it.
            if (to_hit < 0.13f && pred.hit) {
                el.u = std::max(el.u, 0.6f);
                el.zeta = 1.2f;
            }
            drive_of(hand).u = std::max(drive_of(hand).u, 0.15f);
            out.hand_mark[i] = b[upper].p + turn_by(chest, aim_[i]) * arm_len;
            continue;
        }
        if (!going_down) aim_[i] = {};
    }

    for (int s = 0; s < 2; ++s) out.foot_mark[static_cast<std::size_t>(s)] = b[s ? r_foot : l_foot].p;

    // ---- the board ---------------------------------------------------------------------------------------------------
    // Going down with his feet still on the board, he kicks it away (decided 170 to 300 ms into the bail, once): the foot
    // on it, the one ahead the way it goes, pushes it out, the knee straightening in a tenth of a second or two; away from
    // where he lands (out in front going over backwards). The board leaves him at 3 m/s unless it is going 2 m/s or more
    // already. When the runtime can set the board's speed, the board gets that much and the foot as much back (2.5 kg against
    // his 80: about 0.1 m/s, nothing to see); otherwise the foot pushes it through the game's own contact.
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
        auto& h = drive_of(thigh);
        h.goal = true;
        h.want = swing_goal(thigh, normalized(turn_back(frame_of(thigh), knee_at - hip)));
        h.wn = 14;
        h.zeta = 0.8f;
        h.u = 0.75f;
        auto& k = drive_of(shin);
        k.goal = true;
        k.want = rotation(V3{0, 0, 20 * deg}); // straight but for 20 degrees (from 60 to 90)
        k.wn = 16;
        k.zeta = 0.75f;
        k.u = 0.75f;
        drive_of(foot).u = std::max(drive_of(foot).u, 0.3f);
        out.kick_leg = kick_leg_;
        out.foot_mark[static_cast<std::size_t>(s)] = kick_target_;
    }

    // ---- what hurts ----------------------------------------------------------------------------------------------------
    // Once he is down, by what hurts most: the hands go to it and press on it (from where they lay, along a minimum-jerk
    // path, there in about half a second), and, as far as Curl up when hurt says (and the harder the hit, the further), he
    // curls up over it. Letting go, it all eases out over a second or more.
    const bool letting_go = let_go_at_ >= 0;
    std::array<bool, part_count> no_crawl{};
    std::array<bool, 2> lifted{};
    // A hand reaching for what hurts is lifted off the ground and written with its arm. Left planted (not written, as
    // every hand on the ground is), the game's wrist joint held the arm to it: lying down the game kept 3-8% of the
    // arm's turns (the recordings), the hand stopped about 0.4 m short and the arms hung in the air.
    std::array<bool, 2> reaching{};
    if (pain || letting_go) {
        const float hold = letting_go ? 1.0f - smooth(static_cast<float>(t_ - let_go_at_) / let_go) : 1.0f;
        const float since = static_cast<float>(t_ - pain_since_);
        const float quick = std::sqrt(reaction);
        const float curl = tuck_k * (racked_now ? 1.0f : 0.5f + 0.5f * severity);
        const int hurt_side = side_of(hurt_);
        // A breath every 2.5 s or so while he holds it: the back a little more and less curled.
        const float breath = 0.035f * std::sin(2 * pi * 0.4f * t + rock_phase_);
        // A point on a body's surface near where it was hit, pressed a centimetre in: on its bone `from`..`to` of the way
        // along it. When that side now faces the ground (he lies on it: no hand gets under him) the side of it towards
        // `hand_side` (+1 his left) and up a little instead.
        const auto surface_spot = [&](Part p, float from, float to, int hand_side) {
            const Part next = child_of(p);
            const float len = next != board ? length(b[next].p - b[p].p) : 2 * body::shapes[p].along;
            const V3 x = b[p].r[0];
            const V3 hit_at = hurt_local_known_ && p == hurt_part_ ? b[p].p + turn_by(b[p].r, hurt_local_) : centre(p) + world_up * 0.1f;
            const V3 on_bone = b[p].p + x * std::clamp(dot(hit_at - b[p].p, x), from * len, to * len);
            V3 radial = across(hit_at - on_bone, x);
            if (length(radial) < 0.01f || dot(normalized(radial), world_up) < -0.35f)
                radial = across(chest[2] * static_cast<float>(hand_side) + world_up * 0.6f, x);
            return on_bone + normalized(radial, world_up) * std::max(body::shapes[p].radius - 0.01f, 0.02f);
        };
        // A hand to a spot (world), pressed there: the arm the two-bone way, the shoulder swinging along a minimum-jerk path
        // from where the arm lay and the elbow bending with it. A spot still out of the arm's reach a second after it should
        // have got there (a knee that did not come up, a hip he lies on) lets the arm down to rest (an arm held up in the air
        // reads as floating).
        const auto hand_to = [&](int s, V3 spot, float u, float duration) {
            const auto i = static_cast<std::size_t>(s);
            const Part upper = s ? r_arm : l_arm, fore = s ? r_forearm : l_forearm, hand = s ? r_hand : l_hand;
            // A hand pinned under him (on the ground, under his chest) stays there: pulling it out would only push him
            // along the ground.
            {
                const V3 at = centre(hand), a0 = flat(centre(pelvis)), a1 = flat(centre(chest3));
                const V3 along = a1 - a0;
                const float f = std::clamp(dot(flat(at) - a0, along) / std::max(dot(along, along), 1e-4f), 0.0f, 1.0f);
                const bool under = length(flat(at) - (a0 + along * f)) < 0.18f && at[1] < centre(chest2)[1] - 0.03f;
                if ((b[hand].touching || b[fore].touching) && under) return;
            }
            const V3 shoulder = b[upper].p;
            const float a = length(b[fore].p - shoulder), c = length(b[hand].p - b[fore].p) + 0.07f;
            const V3 toward = normalized(spot - shoulder, -chest[0]);
            // The elbow out to his side and down: lying there it rests on the ground, never pointing up into the air.
            const V3 pole = chest[2] * (s == 0 ? 1.0f : -1.0f) - world_up * 0.7f;
            const V3 elbow_at = reach_middle(shoulder, spot, a, c, pole);
            const float d = std::clamp(length(spot - shoulder), std::abs(a - c) + 0.02f, (a + c) * 0.98f);
            const float bend = pi - std::acos(std::clamp((a * a + c * c - d * d) / (2 * a * c), -1.0f, 1.0f));
            const float along = min_jerk(since * quick / duration);
            if (gave_up_at_[i] < 0 && since > duration / quick + 1.0f && length(spot - shoulder) > (a + c) * 1.05f) gave_up_at_[i] = t_;
            const float rest = gave_up_at_[i] >= 0 ? 1.0f - smooth(static_cast<float>(t_ - gave_up_at_[i]) / 0.5f) : 1.0f;
            reaching[i] = rest > 0.05f;
            // The upper arm's whole turn, its twist too: its bone to the elbow, and its y axis to the hand's side of it, so
            // the elbow's bend (about the upper arm's z, +z bringing the forearm towards its y) folds the forearm to the
            // spot. Swung with its twist left as it lay, the elbow folded in whatever plane that left: lying on his back
            // the forearm stood up in the air and the hand hung 0.4 m off what it reached for.
            const V3 ux = normalized(elbow_at - shoulder, toward);
            V3 uy = across(spot - elbow_at, ux);
            if (length(uy) < 1e-3f) uy = across(b[upper].r[1], ux);
            uy = normalized(uy, b[upper].r[1]);
            const M3 frame_r = b[ranges()[index_of()[upper]].frame].r;
            const M3 want_rel = multiply(transposed(frame_r), M3{ux, uy, cross(ux, uy)});
            const M3& from_rel = base_[index_of()[upper]];
            const V3 turn = rotation_vector(multiply(want_rel, transposed(from_rel)));
            auto& sh = drive_of(upper);
            sh.goal = true;
            sh.want = multiply(rotation(turn * along), from_rel);
            sh.wn = 8;
            sh.zeta = 1;
            sh.u = u * hold * rest;
            sh.carry = hold * rest * smooth(3 * along); // taken on as the arm sets off
            auto& el = drive_of(fore);
            el.goal = true;
            el.want = rotation(V3{0, 0, lerp(bend_of(base_[index_of()[fore]]), bend, along)});
            el.wn = 8;
            el.zeta = 1;
            el.u = 0.85f * u * hold * rest;
            el.carry = sh.carry;
            out.hand_mark[i] = spot;
        };
        // His belly, a little to one side (`side`: +1 his left), on its surface: where a hurt arm's hand is held in.
        const auto belly = [&](int side) {
            return centre(back1) + chest[1] * (body::shapes[back1].radius - 0.01f) + chest[2] * (0.05f * static_cast<float>(side));
        };
        // A leg drawn up off the ground towards his chest: the thigh towards `dir` (the pelvis's axes), the knee bent to
        // `bend`, `amount` of the way from how it lay (the hugged knee, the racked curl). The heel comes in first and the
        // knee up after it (a straight leg swung up would be a pike), and the foot comes with the leg (it is not left
        // planted on the ground).
        const auto leg_up = [&](int s, V3 dir, float bend, float amount, float u, float duration) {
            if (amount < 0.01f) return;
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin;
            const float along = min_jerk(since / duration) * std::clamp(amount, 0.0f, 1.0f);
            const float heel = min_jerk(since / (0.6f * duration)) * std::clamp(amount, 0.0f, 1.0f);
            lifted[static_cast<std::size_t>(s)] = true;
            const V3 from = normalized(base_[index_of()[thigh]][0], V3{-1, 0, 0});
            const float apart = std::acos(std::clamp(dot(from, normalized(dir)), -1.0f, 1.0f));
            auto& h = drive_of(thigh);
            h.goal = true;
            h.want = swing_goal(thigh, turn_towards(from, normalized(dir), apart * along));
            h.wn = 7;
            h.zeta = 1;
            h.u = u * hold;
            h.carry = hold * smooth(3 * along);
            auto& k = drive_of(shin);
            k.goal = true;
            const float knee_from = bend_of(base_[index_of()[shin]]);
            k.want = rotation(V3{0, 0, lerp(knee_from, std::max(knee_from, bend), heel)});
            k.wn = 10;
            k.zeta = 1;
            k.u = 0.8f * u * hold;
            k.carry = hold * smooth(3 * heel);
            out.leg_lifted[static_cast<std::size_t>(s)] = along > 0.05f;
        };
        // A knee drawn up from how it lay with its foot staying on the ground (a leg whose foot is off the ground is left
        // as it lies: no leg is lifted lying down), and never straightened against the ground (no crawling).
        const auto knee_up = [&](int s, float hip, float knee, float u) {
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin, foot = s ? r_foot : l_foot, toe = s ? r_toe : l_toe;
            if ((!b[foot].touching && !b[toe].touching) || hip + knee < 0.01f) return;
            const float along = min_jerk(since / 0.9f);
            auto& h = drive_of(thigh);
            h.goal = true;
            h.want = multiply(rotation(V3{0, 0, -hip * along}), base_[index_of()[thigh]]); // -z swings a thigh forwards
            h.wn = 4;
            h.zeta = 1;
            h.u = u * std::max(hold, 0.3f);
            h.no_push = b[pelvis].r[2];
            auto& k = drive_of(shin);
            k.goal = true;
            const float knee_from = bend_of(base_[index_of()[shin]]);
            k.want = rotation(V3{0, 0, std::max(knee_from, std::min(knee_from + knee * along, 100 * deg))});
            k.wn = 4;
            k.zeta = 1;
            k.u = u * std::max(hold, 0.3f);
            k.no_push = -tree.axis[shin];
            no_crawl[thigh] = no_crawl[shin] = true;
        };
        // The back curled from how he lay: forwards (`flex`), to his left (`side`), turned to his left (`twist`), spread
        // over its four joints, with his breathing.
        const auto curl_back = [&](float flex, float side, float twist, float u, float duration) {
            if (std::abs(flex) + std::abs(side) + std::abs(twist) < 0.01f) return;
            const float along = min_jerk(since / duration);
            for (const Part p : {spine, back1, chest2, chest3}) {
                auto& d = drive_of(p);
                d.goal = true;
                // +y turns the spine's top to his right: a bend to his left is -y.
                d.want = multiply(rotation(V3{0.25f * twist * along, -0.25f * side * along, (0.25f * flex + 0.25f * breath) * along}),
                                  base_[index_of()[p]]);
                d.wn = 4;
                d.zeta = 1;
                d.u = u * std::max(hold, 0.3f);
            }
        };
        const auto neck_bend = [&](float flex, float turn, float u) {
            if (std::abs(flex) + std::abs(turn) < 0.01f) return;
            const float along = min_jerk(since / 0.8f);
            for (const Part p : {neck, head}) {
                auto& d = drive_of(p);
                d.goal = true;
                d.want = multiply(rotation(V3{turn * 0.5f * along, 0, flex * (p == neck ? 0.65f : 0.35f) * along}), base_[index_of()[p]]);
                d.wn = 5;
                d.zeta = 1;
                d.u = std::max(d.u, u * hold);
            }
        };
        const int hurt_arm = hurt_side > 0 ? 0 : 1, other_arm = 1 - hurt_arm;
        switch (hurt_) {
        case Hurt::head: {
            // Both hands to his head (his face, or the back of it if that is where it hit), the knees drawn up, curled.
            // His face, or the back of his head if that is where it hit; lying on his face, the sides of his head.
            const bool behind = hurt_local_known_ && hurt_local_[1] < 0.0f; // its centre is 2.5 cm in front of its joint
            const V3 face = b[head].r[1];
            const bool face_down = dot(face, world_up) < -0.3f;
            for (int s = 0; s < 2; ++s) {
                const float side = s == 0 ? 1.0f : -1.0f;
                const V3 spot = face_down ? centre(head) + b[head].r[2] * (0.09f * side) + b[head].r[0] * 0.03f
                                          : centre(head) + face * (behind ? -0.09f : 0.09f) + chest[2] * (0.05f * side);
                hand_to(s, spot, 0.45f, 0.45f);
            }
            neck_bend(25 * deg * curl, 0, 0.3f);
            curl_back(0.5f * curl, 0, 0, 0.15f, 0.9f);
            for (int s = 0; s < 2; ++s) knee_up(s, 0.6f * curl, 1.0f * curl, 0.2f);
            break;
        }
        case Hurt::left_arm:
        case Hurt::right_arm: {
            // The hurt arm held in, its forearm across his belly (the elbow at his side, bent), and the other hand cupping
            // the elbow (the shoulder, for a collarbone) from the inside; he curls a little towards it, looking at it.
            const int s = hurt_arm;
            const Part fore = s ? r_forearm : l_forearm, upper = s ? r_arm : l_arm;
            const bool shoulder = hurt_part_ == l_collar || hurt_part_ == r_collar;
            hand_to(s, belly(0), 0.3f, 0.5f);
            // Where it was hit: the shoulder, along the upper arm down to the elbow, or the forearm from the elbow tip
            // to its middle.
            const V3 spot = shoulder ? surface_spot(upper, 0.0f, 0.15f, -hurt_side)
                            : hurt_part_ == upper ? surface_spot(upper, 0.35f, 1.0f, -hurt_side)
                                                  : surface_spot(fore, 0.0f, 0.6f, -hurt_side);
            hand_to(other_arm, spot, 0.45f, 0.5f);
            curl_back(0.35f * curl, 0.17f * curl * static_cast<float>(hurt_side), 0, 0.15f, 0.9f);
            neck_bend(0.4f * curl, 0.3f * curl * static_cast<float>(hurt_side), 0.25f);
            // In pain he does not lie flat: the knees come up a little, feet on the ground.
            for (int k = 0; k < 2; ++k) knee_up(k, 0.4f * curl, 0.7f * curl, 0.2f);
            break;
        }
        case Hurt::left_hand:
        case Hurt::right_hand: {
            // The hurt hand pulled in to his belly and the other forearm folded over it, both resting on him; curled over
            // it, knees up. (Not the other hand round the hurt wrist: two hands chasing each other through the air is the
            // one reach the ragdoll never made; in the user's game it only raised the arms a little.)
            const int s = hurt_arm;
            hand_to(s, belly(-hurt_side), 0.35f, 0.45f);
            hand_to(other_arm, belly(-hurt_side) + chest[1] * 0.03f, 0.4f, 0.5f);
            curl_back(0.5f * curl, 0, 0, 0.15f, 0.9f);
            for (int k = 0; k < 2; ++k) knee_up(k, 0.5f * curl, 0.8f * curl, 0.2f);
            break;
        }
        case Hurt::left_leg:
        case Hurt::right_leg: {
            // That knee drawn up to his chest (the one time a leg comes off the ground lying down) and both hands on it
            // (the knee, or the thigh where it was hit; a hand that cannot get there rests after a second); the other knee
            // drawn up a little, its foot down.
            const int s = hurt_side > 0 ? 0 : 1;
            const float out_z = s == 0 ? 1.0f : -1.0f;
            const Part thigh = s ? r_thigh : l_thigh, shin = s ? r_shin : l_shin;
            leg_up(s, V3{0.42f, 0.9f, 0.05f * out_z}, 120 * deg, curl, 0.35f, 0.8f);
            // Where it was hit: along the thigh, or from the kneecap down to the middle of the shin.
            const V3 spot = hurt_part_ == thigh ? surface_spot(thigh, 0.3f, 0.85f, hurt_side) : surface_spot(shin, 0.02f, 0.5f, hurt_side);
            for (int k = 0; k < 2; ++k) hand_to(k, spot + chest[2] * (k == 0 ? 0.04f : -0.04f), 0.45f, 0.5f);
            knee_up(1 - s, 0.5f * curl, 0.8f * curl, 0.2f);
            curl_back(0.4f * curl, 0, 0, 0.15f, 0.9f);
            neck_bend(0.4f * curl, 0, 0.25f);
            break;
        }
        case Hurt::left_hip:
        case Hurt::right_hip:
        case Hurt::back: {
            // A hand pressed to the spot (the same side's), knees up, turning over onto the other side.
            const int side = hurt_side != 0 ? hurt_side : (roll_side_ > 0 ? -1 : 1);
            const int s = side > 0 ? 0 : 1;
            hand_to(s, surface_spot(hurt_part_, 0.1f, 0.9f, side), 0.4f, 0.55f);
            for (int k = 0; k < 2; ++k) knee_up(k, 0.6f * curl, 1.0f * curl, 0.2f);
            curl_back(0.25f * curl, 0, -0.25f * curl * static_cast<float>(side), 0.15f, 1.0f);
            break;
        }
        case Hurt::left_ribs:
        case Hurt::right_ribs: {
            // That side's hand (the forearm across him) pressed to the ribs, the back curled and bent over them.
            const int s = hurt_arm;
            hand_to(s, surface_spot(hurt_part_, 0.1f, 0.9f, hurt_side), 0.4f, 0.5f);
            curl_back(0.6f * curl, 0.25f * curl * static_cast<float>(hurt_side), 0, 0.18f, 0.9f);
            neck_bend(0.3f * curl, 0, 0.2f);
            break;
        }
        case Hurt::groin: {
            // Racked, the worst of all: both hands between his thighs, clamped there (squeezing harder every few seconds);
            // the thighs squeezed together and drawn up (60 to 90 degrees by half a second), then all the way into a ball
            // (knees to the chest, back rounded, head down) by two seconds; then on his side, rolling to and fro.
            const V3 groin = centre(pelvis) - b[pelvis].r[0] * 0.12f + b[pelvis].r[1] * 0.07f;
            const float squeeze = 0.2f * std::pow(std::max(0.0f, std::sin(2 * pi * t / 3.1f + rock_phase_)), 4.0f);
            for (int s = 0; s < 2; ++s) hand_to(s, groin + b[pelvis].r[2] * (s == 0 ? 0.03f : -0.03f), 0.75f + squeeze, 0.35f);
            const float full = min_jerk((since - 0.35f) / 1.2f);
            const float rolling = smooth((since - 1.8f) / 0.8f);
            const float roll = rolling * (0.25f * std::sin(2 * pi * t / 2.4f + rock_phase_) + 0.1f * std::sin(2 * pi * t / 1.5f + 2 * rock_phase_)) +
                               0.2f * rolling * static_cast<float>(roll_side_);
            for (int s = 0; s < 2; ++s) {
                const float out_z = s == 0 ? 1.0f : -1.0f;
                const float hip_deg = lerp(75, 115, full) * deg;
                // Squeezed together (towards the middle), and both knees tipped the way he rolls.
                const V3 dir{-std::cos(hip_deg), std::sin(hip_deg), -0.12f * out_z + 0.5f * roll};
                leg_up(s, dir, lerp(75, 130, full) * deg, curl, 0.45f, 0.4f);
            }
            curl_back(lerp(0.35f, 0.9f, full) * curl, 0, 0.4f * roll * curl, 0.25f, 0.6f);
            neck_bend(lerp(0.2f, 0.6f, full) * curl, 0.5f * roll, 0.3f);
            break;
        }
        case Hurt::none: break;
        }
    }
    // In the reaction, the legs' joints never push the leg straight against the ground: a hip or knee whose foot is down
    // does not extend (the curl only draws it up).
    for (std::size_t k = 0; k < joint_count; ++k) {
        const Part c = ranges()[k].child;
        if (!no_crawl[c]) drive[k].no_push = {};
    }

    // ---- the muscles ---------------------------------------------------------------------------------------------
    // Each joint's activation follows what it is asked (40 ms to tense, 100 ms to let go). Tone is a damper on the
    // joint's own turning (or towards a reflex's turning); a goal is a spring and damper towards it. Both are set against
    // the joint's inertia as the free linked bodies present it, so a damping rate or a natural frequency means what it
    // says whatever hangs off the joint, and the whole body's are solved together in one step as a stable PD (Tan, Liu and
    // Turk 2011: taken at the end of the step, so no stiffness makes it ring), the linked bodies moving as Featherstone's
    // method has them (real_bails_body.h). A joint whose torque comes out over what it has (activation x its strength), or
    // that would push a way it must not, is held to what it may and the rest solved again.
    const std::array<M3, part_count> apparent = tree.respond();
    // A hand, foot or toes on the ground is the ground's: it is not written (forced, a light end body flicks round), and
    // its own joint is left to the ground and the game (a torque there would only push the forearm or shin against an
    // end that does not move with it). Not the kicking foot: it pushes the board.
    std::array<bool, part_count> planted{}, written_body{};
    const Part kick_foot = kick_leg_ > 0 ? r_foot : l_foot, kick_toe = kick_leg_ > 0 ? r_toe : l_toe;
    for (std::size_t i = 1; i < part_count; ++i) {
        const auto p = static_cast<Part>(i);
        const bool lifting = (lifted[0] && (p == l_foot || p == l_toe)) || (lifted[1] && (p == r_foot || p == r_toe)) ||
                             (reaching[0] && p == l_hand) || (reaching[1] && p == r_hand);
        planted[i] = is_end(p) && b[i].touching && !(kicking && (p == kick_foot || p == kick_toe)) && !lifting;
        written_body[i] = !planted[i];
    }
    // A hurt arm is weaker for the rest of the bail (down to 40% of its strength as its hurt grows): it does not take his
    // weight again and folds if it has to.
    std::array<float, part_count> weak{};
    weak.fill(1.0f);
    for (int s = 0; s < 2; ++s) {
        const float hurt = std::max(damage_[static_cast<std::size_t>(s ? Hurt::right_arm : Hurt::left_arm)],
                                    damage_[static_cast<std::size_t>(s ? Hurt::right_hand : Hurt::left_hand)]);
        const float keep = 1.0f - 0.6f * std::clamp(hurt / 3.0f, 0.0f, 1.0f);
        for (const Part p : s ? std::array<Part, 3>{r_arm, r_forearm, r_hand} : std::array<Part, 3>{l_arm, l_forearm, l_hand}) weak[p] = keep;
    }
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
        cap[c] = std::max(act_[k], 0.02f) * body::shapes[c].torque * strength * weak[c];
        no_push[c] = d.no_push;
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
        // What it carries of the weight hanging off it: the torque that holds that up (one side of the joint against the
        // other, as every muscle torque here).
        const V3 lift = tree.hold[c] * (std::clamp(d.carry, 0.0f, 1.0f) * dt);
        if (body::hinge(c)) {
            // An elbow or knee bends about its upper bone's z only (the game holds the other two axes).
            const V3 n = tree.axis[c];
            const float q_rate = dot(rate, n), q_e = dot(e, n), q_spin = dot(d.spin, n);
            push[c] = n * (apparent[c][0][0] * (kp * (q_e - dt * q_rate) - kd * (q_rate - q_spin)) * dt + dot(lift, n));
        } else {
            push[c] = turn_by(apparent[c], (e - rate * dt) * kp - (rate - d.spin) * kd) * dt + lift;
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
    // In the game: each joint's torque, as a person's muscles have it above, is handed over larger by about what the
    // game's joint solve takes back (game_gain), so what stays is that torque. Still one side of each joint against the
    // other through the linked bodies: the body's momentum is kept. (`torque` and `work` stay what the muscles mean.)
    if (in.in_game) {
        std::array<V3, part_count> handed{};
        for (std::size_t k = 1; k < body::chain.size(); ++k) {
            const Part c = body::chain[k];
            if (!body::weld(c) && !planted[c]) handed[c] = torque_of(c) * (dt * gain_of(c));
        }
        tree.articulate(std::array<M3, part_count>{});
        tree.solve(handed, dw, dv, outside);
        // Never more than 6 rad/s added to a body in one step however much the game takes back: if it is more, the whole
        // set is scaled down together (still one side of each joint against the other).
        float most = 0;
        for (std::size_t i = 1; i < part_count; ++i)
            if (written_body[i]) most = std::max(most, length(dw[i]));
        if (most > 6.0f && std::isfinite(most)) {
            for (auto& h : handed) h = h * (6.0f / most);
            tree.solve(handed, dw, dv, outside);
        }
    }

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
    // What each joint was asked, for how much of it the game keeps (next step).
    if (in.in_game) {
        for (const auto& l : ranges()) {
            const Part c = l.child, f = l.frame;
            const V3 now = b[c].w - b[f].w;
            const V3 c_w = written.write[c] ? written.w[c] : b[c].w, f_w = written.write[f] ? written.w[f] : b[f].w;
            before_[c] = now;
            asked_[c] = (c_w - f_w) - now;
        }
        asked_known_ = true;
    }
    return written;
}
}
