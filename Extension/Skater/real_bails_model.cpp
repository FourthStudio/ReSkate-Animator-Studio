#include "real_bails_model.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::real_bails::model {
namespace {
using namespace math;
constexpr float pi = 3.14159265f;
constexpr float deg = pi / 180.0f;
constexpr V3 world_up{0, 1, 0};

// About a 75 kg skater, kg.
constexpr std::array<float, part_count> mass{0, 5, 1.5f, .5f, 1.3f, 2, 1.5f, .5f, 1.3f, 2, 1.5f, 8, 6, 5, 5, .3f, 1, 4, 9, .3f, 1, 4, 9, 11};
// How much of a joint's turn its own side takes (the rest goes the other way round, onto the parent): the share of
// the parent side's inertia, so a hand moves its hand, and the back moves both halves of the body.
constexpr std::array<float, part_count> child_share{0, .8f, .75f, .97f, .93f, .88f, .8f, .97f, .93f, .88f, .8f, .5f, .5f, .5f, .5f,
                                                    .97f, .95f, .9f, .75f, .97f, .95f, .9f, .75f, 0};
// Strength: the most a muscle changes its joint's turning speed, radians per second per second.
constexpr std::array<float, part_count> strength_of{0, 70, 60, 120, 110, 90, 70, 120, 110, 90, 70, 45, 45, 45, 45,
                                                    120, 110, 90, 70, 120, 110, 90, 70, 0};

struct Joint {
    Part child;
    V3 error;    // how the child should turn relative to its parent (world rotation vector)
    float tone;  // 0..1
    float quick{1}; // how fast the muscle may drive the joint, against the usual (the bracing arms are quick)
};

V3 bone(const Input& in, Part from, Part to) noexcept { return in.body[to].p - in.body[from].p; }

// The rotation vector that would put `child`'s axes at `want` (world axes).
V3 turn_to(const M3& now, const M3& want) noexcept { return rotation_vector(multiply(want, transposed(now))); }
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
    }
    return "?";
}

bool Muscles::frame(const Input& in, Frame& f) const noexcept {
    const auto& b = in.body;
    if (!b[pelvis].ok || !b[neck].ok || !b[l_arm].ok || !b[r_arm].ok) return false;
    f.up = normalized(b[neck].p - b[pelvis].p);
    f.right = normalized(across(b[r_arm].p - b[l_arm].p, f.up), {1, 0, 0});
    f.forward = cross(f.up, f.right) * handed_;
    return true;
}

void Muscles::enter(Phase next) noexcept {
    if (phase_ != next) in_phase_ = 0;
    phase_ = next;
}

void Muscles::begin(const Input& in, std::uint32_t seed) noexcept {
    *this = Muscles{};
    started_ = true;
    // A small hash of the seed for each choice, -1..1.
    std::uint32_t h = seed * 2654435761u + 0x9e3779b9u;
    const auto next = [&h] {
        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
        return static_cast<float>(h & 0xffff) / 32767.5f - 1.0f;
    };
    tuck_ = 0.5f + 0.15f * next();
    for (auto& v : vary_) v = next();
    phase_ = Phase::brace;
    const auto& b = in.body;
    for (std::size_t i = 1; i < part_count; ++i)
        if (b[i].ok && b[parent_of[i]].ok) rest_[i] = multiply(transposed(b[parent_of[i]].r), b[i].r);
    // Each hinge from the joint's own bend now (the riding pose keeps knees and elbows bent); a near-straight one takes
    // the upper bone's own z axis.
    const std::array<std::array<Part, 3>, 4> hinges{{{l_arm, l_forearm, l_hand}, {r_arm, r_forearm, r_hand},
                                                     {l_thigh, l_shin, l_foot}, {r_thigh, r_shin, r_foot}}};
    for (std::size_t k = 0; k < hinges.size(); ++k) {
        const auto [upper, middle, end] = hinges[k];
        hinge_[k] = {0, 0, 1};
        if (!b[upper].ok || !b[middle].ok || !b[end].ok) continue;
        const V3 axis = cross(normalized(b[middle].p - b[upper].p), normalized(b[end].p - b[middle].p));
        if (length(axis) > 0.08f) hinge_[k] = turn_back(b[upper].r, normalized(axis));
    }
    // Which way is forward: the knees bend forwards and the toes point forwards. From those, the world's handedness.
    Frame f{};
    if (frame(in, f)) {
        V3 hint{};
        for (const auto [hip, knee, ankle, toe] : {std::array{l_thigh, l_shin, l_foot, l_toe}, std::array{r_thigh, r_shin, r_foot, r_toe}}) {
            if (!b[hip].ok || !b[knee].ok || !b[ankle].ok || !b[toe].ok) continue;
            const V3 mid = (b[hip].p + b[ankle].p) * 0.5f;
            hint = hint + across(b[knee].p - mid, f.up) * 2.0f + across(b[toe].p - b[ankle].p, f.up);
        }
        const float sign = dot(cross(f.up, f.right), hint);
        if (std::abs(sign) > 1e-4f) handed_ = sign > 0 ? 1.0f : -1.0f;
    }
}

Output Muscles::step(const Input& in) noexcept {
    Output out;
    out.phase = phase_;
    const auto& b = in.body;
    for (std::size_t i = 1; i < part_count; ++i)
        if (!b[i].ok || !finite(b[i].w) || !finite(b[i].v) || !finite(b[i].p)) return out;
    Frame f{};
    if (!started_ || !frame(in, f) || !(in.dt > 0) || in.dt > 0.1f) return out;
    const float dt = in.dt;
    t_ += dt;
    in_phase_ += dt;

    // ---- how the body moves --------------------------------------------------------------------------------------
    float total = 0;
    V3 velocity{};
    for (std::size_t i = 1; i < part_count; ++i) { velocity = velocity + b[i].v * mass[i]; total += mass[i]; }
    velocity = velocity * (1.0f / total);
    const float speed = length(velocity);
    const float spin = 0.5f * (length(b[pelvis].w) + length(b[chest3].w));
    bool torso_touch = false, any_touch = false;
    float torso_hit = 0, any_hit = 0;
    for (const Part p : {pelvis, spine, back1, chest2, chest3, l_collar, r_collar, neck, head}) {
        torso_touch = torso_touch || b[p].touching;
        torso_hit = std::max(torso_hit, b[p].impact);
    }
    for (std::size_t i = 1; i < part_count; ++i) {
        any_touch = any_touch || b[i].touching;
        any_hit = std::max(any_hit, b[i].impact);
        // What hit hardest, for the hand to go to afterwards (not the feet or hands: they land on purpose).
        const auto p = static_cast<Part>(i);
        if (p != l_hand && p != r_hand && p != l_foot && p != r_foot && p != l_toe && p != r_toe && p != head && p != neck &&
            b[i].impact > hurt_impact_ + 0.5f) {
            hurt_impact_ = b[i].impact;
            hurt_ = p;
        }
    }
    untouched_for_ = any_touch ? 0 : untouched_for_ + dt;
    still_for_ = speed < 1.0f && spin < 2.0f ? still_for_ + dt : 0;

    // Where it falls: where the body goes over the ground, and where the torso leans.
    const V3 horizontal = across(velocity, world_up);
    const V3 lean = across(f.up, world_up);
    const V3 falling = horizontal * 0.6f + lean * 2.0f;
    if (length(falling) > 0.15f) fall_ = normalized(falling);
    const V3 forward_flat = normalized(across(f.forward, world_up), f.forward);
    const V3 right_flat = normalized(across(f.right, world_up), f.right);
    if (phase_ == Phase::brace && length(fall_) > 0.5f) {
        fall_forward_ = dot(fall_, forward_flat);
        fall_side_ = dot(fall_, right_flat);
    }

    // ---- the phase -----------------------------------------------------------------------------------------------
    const bool airborne = untouched_for_ > 0.2;
    switch (phase_) {
    case Phase::brace:
        if ((torso_touch && torso_hit > 1.2f) || any_hit > 3.5f || (t_ > 0.15 && torso_touch)) enter(Phase::impact);
        break;
    case Phase::impact:
        if (in_phase_ > 0.14) enter(spin > 4.5f ? Phase::tumble : speed > 1.4f ? Phase::slide : Phase::settle);
        break;
    case Phase::tumble:
        if (airborne && velocity[1] < -1.5f) enter(Phase::brace);
        else if (spin < 2.5f && in_phase_ > 0.2) enter(speed > 1.4f ? Phase::slide : Phase::settle);
        break;
    case Phase::slide:
        if (airborne && velocity[1] < -1.5f) enter(Phase::brace);
        else if (spin > 5.5f) enter(Phase::tumble);
        else if (still_for_ > 0.2) enter(Phase::settle);
        break;
    case Phase::settle:
        if (airborne && velocity[1] < -1.5f) enter(Phase::brace);
        else if (spin > 5.0f && speed > 2.0f) enter(Phase::tumble);
        else if (in_phase_ > 1.6) enter(Phase::rest);
        break;
    case Phase::rest:
        if (airborne && velocity[1] < -1.5f) enter(Phase::brace);
        break;
    case Phase::idle: return out;
    }
    if (phase_ == Phase::settle && in_phase_ < dt * 1.5) {
        // Which hand goes to the hurt: the other side's, unless that one is out of reach.
        hurt_hand_ = 0;
        if (hurt_ != board && hurt_impact_ > 3.0f) {
            const V3 at = b[hurt_].p;
            const float left = length(at - b[l_arm].p), right = length(at - b[r_arm].p);
            const bool on_left = hurt_ == l_forearm || hurt_ == l_arm || hurt_ == l_collar;
            const bool on_right = hurt_ == r_forearm || hurt_ == r_arm || hurt_ == r_collar;
            hurt_hand_ = on_left ? 1 : on_right ? -1 : left < right ? -1 : 1;
            if (std::min(left, right) > 0.75f && !on_left && !on_right) hurt_hand_ = 0;
        }
    }

    // ---- tone ----------------------------------------------------------------------------------------------------
    float tone = 0.7f;
    switch (phase_) {
    case Phase::brace: tone = airborne && t_ > 0.35 ? 0.65f : 0.75f; break;
    case Phase::impact: tone = 0.22f; break;
    case Phase::tumble: tone = 0.6f; break;
    case Phase::slide: tone = 0.5f; break;
    case Phase::settle: tone = 0.5f - 0.32f * static_cast<float>(std::min(1.0, in_phase_ / 1.2)); break;
    case Phase::rest: tone = 0.14f; break;
    default: break;
    }
    tone *= std::clamp(in.strength, 0.25f, 2.0f);
    out.tone = tone;
    out.phase = phase_;

    // ---- targets -------------------------------------------------------------------------------------------------
    const V3 down = -world_up;
    const float back_fall = std::max(0.0f, -fall_forward_), front_fall = std::max(0.0f, fall_forward_);
    float spine_flex = 0, head_flex = 0, head_tilt = 0; // radians: + curls forward; + tilts the head to its right
    // The head-righting reflex: the neck turns the head towards the sky whichever way the body lies or falls, so falling
    // back the chin tucks, face first the head tips back and on a side it tilts up, all off the ground.
    const float right_flex = std::atan2(dot(world_up, f.forward), dot(world_up, f.up));
    const float right_tilt = std::atan2(dot(world_up, f.right), dot(world_up, f.up));
    // Hands and feet: where each one goes (world), and the bend side (pole) of the elbow and knee.
    V3 hand[2], foot[2];
    const Part shoulder[2] = {l_arm, r_arm}, elbow[2] = {l_forearm, r_forearm}, wrist[2] = {l_hand, r_hand};
    const Part hip[2] = {l_thigh, r_thigh}, knee[2] = {l_shin, r_shin}, ankle[2] = {l_foot, r_foot};
    float arm_len[2], leg_len[2];
    for (int s = 0; s < 2; ++s) {
        arm_len[s] = length(bone(in, shoulder[s], elbow[s])) + length(bone(in, elbow[s], wrist[s]));
        leg_len[s] = length(bone(in, hip[s], knee[s])) + length(bone(in, knee[s], ankle[s]));
    }
    const V3 out_side[2] = {-f.right, f.right};
    // The head dropping fast at the ground: going over head first.
    const bool pitching = dot(b[head].v, down) > 2.5f && b[head].p[1] < b[pelvis].p[1] + 0.5f;
    switch (phase_) {
    case Phase::brace:
    case Phase::impact: {
        // Hands out to where the body goes down: forwards and down, back and down, to the falling side.
        const bool long_fall = airborne && t_ > 0.35;
        if (long_fall) swing_ += dt * 11.0f;
        for (int s = 0; s < 2; ++s) {
            const float this_side = s == 0 ? -1.0f : 1.0f;
            const float towards = std::max(0.0f, this_side * fall_side_); // this arm is on the falling side
            V3 reach = fall_ * 0.85f + down * 0.9f + out_side[s] * (0.25f + 0.35f * towards);
            if (towards == 0 && std::abs(fall_side_) > 0.6f) reach = reach + f.up * 0.6f; // the high arm stays up
            if (long_fall) {
                // A long fall: the arms go out and up for balance and flail a little, each out of step with the other.
                const float flail = 0.35f * std::sin(static_cast<float>(swing_) + (s ? 2.1f : 0.0f));
                reach = out_side[s] * 0.9f + f.up * (0.25f + flail) + f.forward * (0.25f - 0.5f * flail) + down * 0.2f;
            }
            hand[s] = b[shoulder[s]].p + normalized(reach) * arm_len[s] * 0.88f;
            // Knees bend to soak it up; falling back the hips fold (sitting down); a long fall reaches the feet down.
            // Dropping from height the feet reach down for the ground with the knees ready to give: landed on straight
            // legs the body tips over them like a pole, head first.
            V3 leg = -f.up * 0.82f + f.forward * (0.2f + 0.45f * back_fall);
            const bool reaching_down = airborne && t_ > 0.3 && velocity[1] < -2.0f;
            if (reaching_down) leg = down * 0.9f + f.forward * 0.3f + normalized(horizontal, {}) * 0.15f;
            foot[s] = b[hip[s]].p + normalized(leg) * leg_len[s] * (reaching_down ? 0.76f : 0.86f);
        }
        spine_flex = 0.6f * back_fall + 0.1f * front_fall;
        head_flex = std::clamp(0.3f * (0.75f * back_fall - 0.8f * front_fall) + 0.7f * right_flex, -0.95f, 1.05f);
        head_tilt = std::clamp(0.7f * right_tilt - 0.15f * fall_side_, -0.7f, 0.7f);
        break;
    }
    case Phase::tumble:
        for (int s = 0; s < 2; ++s) {
            // Tucked, not balled up: hands up by the head, knees in towards the chest, one a little more than the other.
            // Pitching over head first the hands go for the ground instead.
            hand[s] = pitching ? b[shoulder[s]].p + normalized(normalized(across(b[head].v, world_up), fall_) * 0.7f + down + out_side[s] * 0.3f) *
                                     arm_len[s] * 0.88f
                               : b[head].p + f.forward * 0.2f + out_side[s] * 0.15f - f.up * 0.08f;
            const float tuck = tuck_ + (s ? vary_[0] : -vary_[0]) * 0.15f;
            foot[s] = b[hip[s]].p + normalized(f.forward * tuck - f.up * 0.75f) * leg_len[s] * (0.82f - 0.2f * tuck);
        }
        spine_flex = 0.35f + 0.25f * tuck_;
        head_flex = 0.65f;
        break;
    case Phase::slide:
    case Phase::settle:
    case Phase::rest: {
        // How he lies decides the pose: face down the legs stay long (bent knees would lift the feet up behind him),
        // on the back the knees come up a little, on a side the top leg bends forward over the bottom one.
        const float face_down = std::max(0.0f, -dot(f.forward, world_up));
        const float on_back = std::max(0.0f, dot(f.forward, world_up));
        const float side_up = dot(f.right, world_up); // + lying on his left side (right side up)
        for (int s = 0; s < 2; ++s) {
            const float this_side = s == 0 ? -1.0f : 1.0f;
            const float upper = std::max(0.0f, this_side * side_up); // this limb is on the upper side
            float bend = 0.15f + 0.35f * on_back + 0.75f * upper + 0.15f * std::abs(side_up) + vary_[1 + s] * 0.2f;
            bend *= 1.0f - 0.8f * face_down;
            if (phase_ == Phase::slide) bend *= 0.6f;
            bend = std::clamp(bend, 0.0f, 0.95f);
            foot[s] = b[hip[s]].p + normalized(-f.up * (1.0f - 0.55f * bend) + f.forward * bend + out_side[s] * (0.08f + 0.1f * face_down)) *
                                        leg_len[s] * (0.97f - 0.22f * bend);
            // Arms: face down they come in under the shoulders (about to push up), on the back they lie out by the sides,
            // on a side the bottom arm reaches forward and the top one lies over the body.
            V3 arm = -f.up * (0.6f + 0.4f * on_back) + out_side[s] * (0.25f + 0.35f * on_back) + f.forward * (0.25f + 0.5f * face_down);
            arm = arm + f.forward * (0.6f * std::max(0.0f, -this_side * side_up)) + (f.forward * 0.3f - out_side[s] * 0.5f) * upper;
            float reach = phase_ == Phase::slide ? 0.65f : 0.78f + 0.12f * on_back;
            if (face_down > 0.5f) reach = 0.55f;
            hand[s] = b[shoulder[s]].p + normalized(arm) * arm_len[s] * reach;
        }
        if (phase_ == Phase::settle && hurt_hand_ != 0 && in_phase_ < 1.4) {
            const int s = hurt_hand_ < 0 ? 0 : 1;
            hand[s] = b[hurt_].p + f.forward * 0.06f;
        }
        spine_flex = phase_ == Phase::slide ? 0.12f : 0.12f + 0.2f * std::abs(side_up);
        // The head held up off the ground while moving, laid down on its side once still: on the back the chin comes in,
        // face down the head turns up, on a side it tilts up.
        const float hold = phase_ == Phase::slide ? 1.0f : phase_ == Phase::settle ? 0.7f : 0.45f;
        head_flex = std::clamp(hold * 0.7f * right_flex, -0.9f, 1.0f) + 0.05f;
        head_tilt = std::clamp(hold * 0.7f * right_tilt, -0.7f, 0.7f);
        break;
    }
    case Phase::idle: break;
    }

    // ---- joints --------------------------------------------------------------------------------------------------
    std::array<Joint, part_count> joints{};
    std::size_t count = 0;
    const auto add = [&](Part child, const V3& error, float joint_tone, float quick = 1.0f) {
        joints[count++] = {child, error, joint_tone, quick};
    };
    // The back curls about the torso's own side axis, shared out over its four joints.
    const V3 flex_axis = normalized(cross(f.up, f.forward));
    const V3 tilt_axis = normalized(cross(f.up, f.right)); // turns the head up towards its right
    for (const Part p : {spine, back1, chest2, chest3}) {
        const M3 want = multiply(rotation(flex_axis, spine_flex * 0.25f), multiply(b[parent_of[p]].r, rest_[p]));
        add(p, turn_to(b[p].r, want), tone * 1.1f);
    }
    {
        const M3 neck_want = multiply(rotation(flex_axis, head_flex * 0.4f), multiply(rotation(tilt_axis, head_tilt * 0.4f),
                                                                                     multiply(b[chest3].r, rest_[neck])));
        // The neck stays tense through the impact (the head is what a falling person guards), whatever the limbs do.
        const float neck_tone = std::max(tone, phase_ == Phase::impact || phase_ == Phase::brace ? 0.7f * std::clamp(in.strength, 0.25f, 2.0f) : 0.0f);
        add(neck, turn_to(b[neck].r, neck_want), neck_tone * 1.2f);
        const M3 head_want = multiply(rotation(flex_axis, head_flex * 0.6f), multiply(rotation(tilt_axis, head_tilt * 0.6f),
                                                                                     multiply(b[neck].r, rest_[head])));
        add(head, turn_to(b[head].r, head_want), neck_tone * 1.2f);
    }
    for (const Part p : {l_collar, r_collar, l_hand, r_hand, l_foot, r_foot, l_toe, r_toe})
        add(p, turn_to(b[p].r, multiply(b[parent_of[p]].r, rest_[p])), tone * 0.8f);
    // Arms and legs: the elbow and knee placed for the hand and foot to reach their marks, bent the way they bend.
    for (int s = 0; s < 2; ++s) {
        // The middle joint is a hinge: it is driven to the bend the reach needs, about its own axis only (asking it to
        // swing any other way is taken straight back out by the joint).
        const auto bend_to = [&](Part upper, Part middle, Part end, const V3& target, const V3& axis_local) {
            const float a = length(bone(in, upper, middle)), c = length(bone(in, middle, end));
            const float d = std::clamp(length(target - b[upper].p), std::abs(a - c) + 0.02f, (a + c) * 0.995f);
            const float wanted = pi - std::acos(std::clamp((a * a + c * c - d * d) / (2 * a * c), -1.0f, 1.0f));
            const V3 u = normalized(bone(in, upper, middle)), l = normalized(bone(in, middle, end));
            const V3 axis = normalized(turn_by(b[upper].r, axis_local));
            const float now = std::atan2(dot(cross(u, l), axis), dot(u, l));
            return axis * (wanted - now);
        };
        // Bracing, the arms shoot out: a protective reach is one of the quickest moves a body makes (about 10 rad/s at the
        // shoulder within a fifth of a second), and they have most of a fall to get there.
        // Through the impact too: the arms keep catching while the legs and back give.
        const bool bracing = phase_ == Phase::brace || phase_ == Phase::impact || (phase_ == Phase::tumble && pitching);
        // Settling, the arms are drawn in (under the shoulders face down, by the sides on the back) before the body lets go.
        const float muscle = std::clamp(in.strength, 0.25f, 2.0f);
        const float arm_tone = bracing ? std::max(tone, 0.75f * muscle) * 1.3f
                             : phase_ == Phase::settle ? std::max(tone, 0.45f * muscle) : tone;
        const float arm_quick = bracing ? 2.6f : 1.0f;
        const V3 elbow_pole = -f.forward * 0.7f + out_side[s] * 0.5f - f.up * 0.3f;
        const V3 e = reach_middle(b[shoulder[s]].p, hand[s], length(bone(in, shoulder[s], elbow[s])),
                                  length(bone(in, elbow[s], wrist[s])), elbow_pole);
        add(shoulder[s], swing_between(normalized(bone(in, shoulder[s], elbow[s])), normalized(e - b[shoulder[s]].p)), arm_tone, arm_quick);
        add(elbow[s], bend_to(shoulder[s], elbow[s], wrist[s], hand[s], hinge_[s]), arm_tone, arm_quick);
        const V3 knee_pole = f.forward + out_side[s] * 0.15f;
        const V3 k = reach_middle(b[hip[s]].p, foot[s], length(bone(in, hip[s], knee[s])), length(bone(in, knee[s], ankle[s])), knee_pole);
        add(hip[s], swing_between(normalized(bone(in, hip[s], knee[s])), normalized(k - b[hip[s]].p)), tone);
        add(knee[s], bend_to(hip[s], knee[s], ankle[s], foot[s], hinge_[2 + s]), tone);
    }

    // ---- muscles -------------------------------------------------------------------------------------------------
    std::array<V3, part_count> w{}, v{};
    for (std::size_t i = 1; i < part_count; ++i) { w[i] = b[i].w; v[i] = b[i].v; }
    // Each body's centre of mass: along its bone (its own x axis, as the game's bones run) a little short of halfway.
    std::array<V3, part_count> centre{};
    for (std::size_t i = 1; i < part_count; ++i) {
        float reach = 0.08f;
        for (std::size_t k = 1; k < part_count; ++k)
            if (parent_of[k] == i && k != i) { reach = std::max(reach, length(b[k].p - b[i].p)); break; }
        centre[i] = b[i].p + b[i].r[0] * (reach * 0.45f);
    }
    const float strength = std::clamp(in.strength, 0.25f, 2.0f);
    for (std::size_t j = 0; j < count; ++j) {
        const auto& joint = joints[j];
        const Part c = joint.child, p = parent_of[c];
        const float t = std::clamp(joint.tone, 0.0f, 1.5f);
        if (t <= 0 || !finite(joint.error)) continue;
        // The speed the joint should turn at to close its error, and how much of the way the muscle gets there now.
        const V3 wanted = clamp_length(joint.error * (7.0f + 3.0f * (joint.quick - 1.0f)), 9.0f * std::sqrt(joint.quick));
        const V3 relative = w[c] - w[p];
        const float gain = 1.0f - std::exp(-t * 30.0f * dt);
        const V3 change = clamp_length((wanted - relative) * gain, strength_of[c] * strength * t * dt * 1.6f * joint.quick);
        // In the air the game holds the body upright itself; a muscle there moves its limb without turning the torso the
        // other way (with the turn shared, reaching the legs down pitched the body over onto its head).
        const float share = airborne && phase_ == Phase::brace ? 1.0f : child_share[c];
        // Both sides turn about the joint they share (the child's own origin), not about their middles: a body spun
        // about its middle pulls its joint apart and the game's joint solve takes the turn straight back out.
        const V3 at = b[c].p;
        const V3 turn_child = change * share, turn_parent = change * -(1.0f - share);
        w[c] = w[c] + turn_child;
        w[p] = w[p] + turn_parent;
        v[c] = v[c] + cross(turn_child, centre[c] - at);
        v[p] = v[p] + cross(turn_parent, centre[p] - at);
    }
    for (std::size_t i = 1; i < part_count; ++i) {
        w[i] = clamp_length(w[i], 25.0f);
        const V3 d = w[i] - b[i].w;
        if (dot(d, d) > 1e-6f) {
            out.w[i] = w[i];
            out.v[i] = clamp_length(v[i], 40.0f);
            out.write[i] = true;
        }
    }
    return out;
}
}
