#include "real_bails.h"
#include "real_bails_model.h"
#include "client_source_spawn.h"
#include "no_bail.h"
#include "skater_physics.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Extension/Profile/local_profile_runtime.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/no_bail.h"
#include "Engine/Game/Build/20260929/skater_body.h"
#include "Engine/Game/Build/20260929/skater_state.h"
#include "Engine/Game/Build/20260929/offboard_flight.h"
#include "Engine/Game/Build/20260929/ragdoll_bodies.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <string>
#include <share.h>
#include <memory>
#include <mutex>
#include <vector>

namespace dingosdk::real_bails {
namespace {
namespace state = addr::skater_state;
using logging::Channel;
using logging::Level;
using game::Vec3;

constexpr std::size_t body_count = state::rig_body_count; // 26: the 24 named ones (skater_body.h) and two more
constexpr std::uint32_t wipeout_state = addr::no_bail::wipeout_physics_state;
constexpr std::uint32_t offboard_state = addr::no_bail::offboard_physics_state;

// One body as read in a physics step: its world transform (Frostbite rows: right, up, forward, then where it is),
// its velocity and spin, and its pending flags.
struct Body {
    std::uintptr_t at{};
    bool ok{};
    std::array<float, 16> m{};
    Vec3 v{}, w{};
    std::uint32_t flags{};
};
struct Bodies {
    std::uintptr_t physics{};
    std::array<Body, body_count> body{};
};

bool finite(const Vec3& v) noexcept {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 1000; });
}
bool read_bodies(const NoBailSkater& skater, Bodies& out) noexcept {
    std::uintptr_t physics{}, type{}, bodies{};
    std::uint32_t count{};
    if (!memory::peek(skater.rig + state::rig_physics_offset, physics) || !memory::peek(physics, type) ||
        type != skater.base + state::rig_physics_vtable || !memory::peek(physics + state::physics_bodies_offset, bodies) ||
        !memory::peek(bodies, count) || count != body_count)
        return false;
    out.physics = physics;
    for (std::size_t i = 1; i < body_count; ++i) {
        auto& b = out.body[i];
        b = {};
        b.at = bodies + i * state::physics_body_size;
        std::uintptr_t owner{};
        if (!memory::peek(b.at + state::body_owner_offset, owner) || owner != physics ||
            !memory::peek(b.at + addr::skater_body::body_transform_offset, b.m) ||
            !memory::peek(b.at + state::body_velocity_offset, b.v) || !memory::peek(b.at + state::body_spin_offset, b.w) ||
            !memory::peek(b.at + state::body_pending_offset, b.flags) || !finite(b.v) || !finite(b.w))
            continue;
        bool valid = true;
        for (float x : b.m) valid = valid && std::isfinite(x) && std::abs(x) < 100000;
        b.ok = valid;
    }
    return true;
}
// Which off-board substate runs (its offset in the machine), 0 when none can be read.
std::uint32_t read_substate(const NoBailSkater& skater) noexcept {
    std::uintptr_t machine{}, active{};
    if (!memory::peek(skater.core + state::offboard_machine_offset, machine) || !machine ||
        !memory::peek(machine + state::offboard_active_offset, active) || active < machine || active - machine > 0x2000)
        return 0;
    return static_cast<std::uint32_t>(active - machine);
}

// ---- the physics world's own bodies (ragdoll_bodies.h) ------------------------------------------------------------
namespace world = addr::ragdoll_bodies;
using SetMotion = void(__fastcall*)(const world::BodyHandle*, const float*);
struct WorldCalls {
    SetMotion set_spin{}, set_velocity{};
    bool ok{};
};
WorldCalls& world_calls() { static WorldCalls value; return value; }
bool start_world(std::uintptr_t base) noexcept {
    for (const auto& contract : {world::set_linear_velocity, world::set_angular_velocity, world::handle_valid}) {
        std::array<unsigned char, 32> actual{};
        if (!memory::peek(base + contract.rva, actual) || actual != contract.bytes) {
            logging::log(Level::warning, Channel::skater, "Real Bails: the physics world's setter at {:#x} did not match.", contract.rva);
            return false;
        }
    }
    auto& w = world_calls();
    w.set_spin = reinterpret_cast<SetMotion>(base + world::set_angular_velocity.rva);
    w.set_velocity = reinterpret_cast<SetMotion>(base + world::set_linear_velocity.rva);
    w.ok = true;
    return true;
}
// A handle the system still holds for its slot.
bool current(const world::BodyHandle& h) noexcept {
    if (h.system < 0x10000 || h.slot == world::no_slot || h.slot > 0x100000) return false;
    std::uintptr_t generations{};
    std::uint32_t generation{};
    return memory::peek(h.system + world::system_generations_offset, generations) && generations &&
           memory::peek(generations + std::uintptr_t{h.slot} * 4, generation) && generation == h.generation;
}
bool call_set(SetMotion set, const world::BodyHandle& h, const Vec3& v) noexcept {
    if (!set || !current(h)) return false;
    alignas(16) float value[4]{v[0], v[1], v[2], 0};
    __try {
        set(&h, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- recording ---------------------------------------------------------------------------------------------------
struct Frame {
    std::uint64_t bail{}; // 0 = not in a bail
    double t{};           // seconds since the bail started
    float dt{};
    std::uint32_t state{}, substate{};
    std::uint8_t ragdoll{}, air{}, phase{};
    std::int8_t step{-1}; // the catch step's leg, +10 once landed
    float tone{};
    std::array<std::array<float, 18>, body_count> body{}; // where, right, up, forward, velocity, spin
    std::array<std::uint32_t, body_count> flags{};
    std::array<std::array<float, 3>, body_count> command{}; // the spin the muscles set (0 = none)
    std::array<std::uint8_t, body_count> ok{};
    std::array<std::uint8_t, skater_body::count> touching{};
    std::array<float, skater_body::count> impact{};
};
constexpr std::size_t ring_size = 2048;
struct Ring {
    std::unique_ptr<Frame[]> frames{new Frame[ring_size]};
    std::atomic<std::uint64_t> head{}, tail{};
    std::atomic<std::uint64_t> dropped{};
};
Ring& ring() { static auto* value = new Ring; return *value; }

struct Shared {
    std::atomic<bool> ready{}, enabled{true}, recording{};
    std::atomic<float> strength{1.0f};
    std::atomic<bool> loaded{}, dirty{}; // the saved settings read; a change to save
    std::mutex tuning_mutex;
    Tuning tuning;
    std::atomic<std::uint64_t> bails{}, driven{};
    std::mutex live_mutex;
    Live live;
};
Shared& shared() { static auto* value = new Shared; return *value; }

// The physics thread's own: the bail it follows and the muscles driving it.
struct Follow {
    bool in_bail{};
    std::uint64_t bail{};
    double t{};
    double out_for{1}; // seconds out of the bail since it was last in one
    model::Muscles muscles;
    Tuning tuning; // the last tuning read
    std::uintptr_t physics{}; // the rig's physics the muscles began on
};
Follow& follow() { static auto* value = new Follow; return *value; }

model::Input muscle_input(const Bodies& bodies, const skater_physics::Step& step, float seconds, float strength, const Tuning& t) noexcept {
    model::Input in;
    in.dt = seconds;
    in.strength = strength;
    in.in_game = true; // the game's joint solve takes most of each push back: the muscles make up for it
    in.tune ={t.reaction, t.head, t.tuck, t.lie_still, t.grip, t.catch_arms, t.catch_step, t.grab_hurt, t.hold_hurt};
    for (std::size_t i = 1; i < model::part_count; ++i) {
        const auto& b = bodies.body[i];
        auto& o = in.body[i];
        o.ok = b.ok;
        if (!b.ok) continue;
        o.p = {b.m[12], b.m[13], b.m[14]};
        o.r = math::from_rows({b.m[0], b.m[1], b.m[2]}, {b.m[4], b.m[5], b.m[6]}, {b.m[8], b.m[9], b.m[10]});
        o.v = b.v;
        o.w = b.w;
        o.touching = step.body.bodies[i].touching;
        o.impact = step.body.bodies[i].impact;
        if (const auto& at = step.body.bodies[i].point) { o.has_point = true; o.point = *at; }
        // The surface's normal tells a floor from a wall: only a floor is the ground he lands on.
        if (const auto& normal = step.body.bodies[i].normal) { o.has_normal = true; o.normal = *normal; }
    }
    return in;
}

// After each physics update of a skater (client_source_spawn.h's body motion callback), on the physics thread.
void on_physics(std::uintptr_t core) noexcept {
    auto& s = shared();
    if (!s.ready.load(std::memory_order_acquire)) return;
    NoBailSkater skater;
    if (!no_bail_skater(skater) || skater.core != core) return; // the local skater only
    float seconds{};
    if (!memory::peek(skater.context + state::step_seconds_offset, seconds) || !std::isfinite(seconds) || seconds <= 0 || seconds > 0.1f)
        return;
    auto& f = follow();
    std::uint32_t physics_state{};
    if (!memory::peek(skater.context + state::physics_state_offset, physics_state)) return;
    const auto step = skater_physics::read_step(skater, seconds, false);
    const bool ragdoll = step.ragdoll.value_or(false);
    const bool in_bail = physics_state == wipeout_state || (physics_state == offboard_state && ragdoll);
    bool fresh = false;
    if (in_bail && !f.in_bail && f.out_for > 0.5) { // a new bail
        f.bail = s.bails.fetch_add(1, std::memory_order_relaxed) + 1;
        f.t = 0;
        fresh = true;
    } else if (in_bail || f.bail) f.t += seconds;
    f.out_for = in_bail ? 0 : f.out_for + seconds;
    f.in_bail = in_bail;
    Bodies bodies;
    const bool have = read_bodies(skater, bodies);
    const auto substate = physics_state == offboard_state ? read_substate(skater) : 0u;

    // The muscles: from the moment the body is a ragdoll until the game gets it up again.
    model::Output driven;
    // Only in the bail's ragdoll (FollowRagdoll): never in FollowAnimatedRagdoll, the blend back to the animation that gets
    // him up, nor FollowSimulatedRagdoll, which no bail was seen in (a stumble the game catches itself is the game's).
    const bool simulated = substate == addr::offboard_flight::offboard_ragdoll_states[0][0];
    const bool muscles = s.enabled.load(std::memory_order_relaxed) && have && in_bail && ragdoll && simulated;
    if (muscles) {
        {
            std::unique_lock lock(s.tuning_mutex, std::try_to_lock);
            if (lock.owns_lock()) f.tuning = s.tuning;
        }
        const auto input = muscle_input(bodies, step, seconds, s.strength.load(std::memory_order_relaxed), f.tuning);
        if (fresh || !f.muscles.started() || f.physics != bodies.physics) {
            f.muscles.begin(input, static_cast<std::uint32_t>(f.bail));
            f.physics = bodies.physics;
        }
        driven = f.muscles.step(input);
        // Through the world's own bodies: the rig's records are only its mirror (ragdoll_bodies.h).
        std::array<world::BodyHandle, world::rig_handle_count> handles{};
        bool wrote = false;
        if (world_calls().ok && memory::peek(skater.core + world::rig_handles_offset, handles)) {
            for (std::size_t i = 1; i < model::part_count && i <= world::rig_handle_count; ++i)
                if (driven.write[i]) {
                    const bool spun = call_set(world_calls().set_spin, handles[i - 1], driven.w[i]);
                    if (spun) (void)call_set(world_calls().set_velocity, handles[i - 1], driven.v[i]);
                    wrote = spun || wrote;
                }
        }
        if (wrote) s.driven.fetch_add(1, std::memory_order_relaxed);
    } else if (!in_bail && f.muscles.started()) {
        f.muscles = {};
    }

    {
        std::unique_lock lock(s.live_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            auto& l = s.live;
            l.valid = true;
            l.state = physics_state;
            l.ragdoll = ragdoll;
            l.in_bail = in_bail;
            l.substate = substate;
            if (step.velocity) (physics_state == offboard_state ? l.pelvis_velocity : l.board_velocity) = *step.velocity;
            l.bails = s.bails.load(std::memory_order_relaxed);
            l.driven = s.driven.load(std::memory_order_relaxed);
            l.bail_seconds = f.bail ? f.t : 0;
            if (have && bodies.body[2].ok && bodies.body[23].ok && bodies.body[5].ok && bodies.body[9].ok) {
                using namespace math;
                const auto at = [&](std::size_t i) { return Vec3{bodies.body[i].m[12], bodies.body[i].m[13], bodies.body[i].m[14]}; };
                const auto up = math::normalized(at(2) - at(23));
                const auto right = math::normalized(math::across(at(9) - at(5), up));
                l.chest_forward = math::cross(up, right);
            }
            l.phase = muscles ? model::name(driven.phase) : in_bail ? "off" : "";
        }
    }

    if (!s.recording.load(std::memory_order_relaxed) || !have) return;
    auto& r = ring();
    const auto head = r.head.load(std::memory_order_relaxed);
    if (head - r.tail.load(std::memory_order_acquire) >= ring_size) { r.dropped.fetch_add(1, std::memory_order_relaxed); return; }
    auto& fr = r.frames[head % ring_size];
    fr.bail = in_bail || (f.bail && f.out_for < 2.5) ? f.bail : 0;
    fr.t = f.t;
    fr.dt = seconds;
    fr.state = physics_state;
    fr.substate = substate;
    fr.ragdoll = ragdoll;
    fr.air = step.airborne;
    fr.phase = static_cast<std::uint8_t>(muscles ? driven.phase : model::Phase::idle);
    fr.tone = muscles ? driven.tone : 0.0f;
    fr.step = static_cast<std::int8_t>(muscles && driven.step >= 0 ? driven.step + (driven.step_landed ? 10 : 0) : -1);
    for (std::size_t i = 0; i < body_count; ++i)
        fr.command[i] = muscles && i < model::part_count && driven.write[i] ? driven.w[i] : std::array<float, 3>{};
    for (std::size_t i = 0; i < body_count; ++i) {
        const auto& b = bodies.body[i];
        fr.ok[i] = b.ok;
        fr.flags[i] = b.flags;
        auto& o = fr.body[i];
        o = {b.m[12], b.m[13], b.m[14], b.m[0], b.m[1], b.m[2], b.m[4], b.m[5], b.m[6], b.m[8], b.m[9], b.m[10],
             b.v[0], b.v[1], b.v[2], b.w[0], b.w[1], b.w[2]};
    }
    for (std::size_t i = 0; i < skater_body::count; ++i) {
        fr.touching[i] = step.body.bodies[i].touching;
        fr.impact[i] = step.body.bodies[i].impact;
    }
    r.head.store(head + 1, std::memory_order_release);
}

// ---- writing recordings out (client thread) ----------------------------------------------------------------------
struct Writer {
    std::filesystem::path folder;
    std::string session;
    std::deque<Frame> before; // the last moments before a bail
    std::uint64_t open_bail{};
    std::FILE* file{};
};
Writer& writer() { static auto* value = new Writer; return *value; }

void write_row(std::FILE* out, const Frame& fr) {
    std::fprintf(out, "%llu,%.5f,%.5f,%u,%u,%u,%u,%u,%.3f,%d", static_cast<unsigned long long>(fr.bail), fr.t, fr.dt, fr.state, fr.substate,
                 fr.ragdoll, fr.air, fr.phase, fr.tone, fr.step);
    for (std::size_t i = 0; i < body_count; ++i) {
        std::fprintf(out, ",%u,%u", fr.ok[i], fr.flags[i]);
        for (float x : fr.body[i]) std::fprintf(out, ",%.5g", x);
        std::fprintf(out, ",%.4g,%.4g,%.4g", fr.command[i][0], fr.command[i][1], fr.command[i][2]);
    }
    for (std::size_t i = 0; i < skater_body::count; ++i) std::fprintf(out, ",%u,%.4g", fr.touching[i], fr.impact[i]);
    std::fputc('\n', out);
}
void write_header(std::FILE* out) {
    std::fputs("bail,t,dt,state,substate,ragdoll,air,phase,tone,step", out);
    static const char* parts[] = {"px", "py", "pz", "rx", "ry", "rz", "ux", "uy", "uz", "fx", "fy", "fz", "vx", "vy", "vz", "wx", "wy", "wz"};
    for (std::size_t i = 0; i < body_count; ++i) {
        std::fprintf(out, ",b%zu_ok,b%zu_flags", i, i);
        for (const char* p : parts) std::fprintf(out, ",b%zu_%s", i, p);
        std::fprintf(out, ",b%zu_cx,b%zu_cy,b%zu_cz", i, i, i);
    }
    for (std::size_t i = 0; i < skater_body::count; ++i) std::fprintf(out, ",c%zu_touch,c%zu_imp", i, i);
    std::fputc('\n', out);
}
void close_file(Writer& w) {
    if (w.file) std::fclose(w.file);
    w.file = nullptr;
    w.open_bail = 0;
}
void drain() {
    auto& r = ring();
    auto& w = writer();
    auto tail = r.tail.load(std::memory_order_relaxed);
    const auto head = r.head.load(std::memory_order_acquire);
    for (; tail != head; ++tail) {
        const Frame& fr = r.frames[tail % ring_size];
        if (!fr.bail) {
            if (w.file) close_file(w);
            w.before.push_back(fr);
            while (w.before.size() > 45) w.before.pop_front();
            continue;
        }
        if (fr.bail != w.open_bail) {
            close_file(w);
            std::error_code error;
            std::filesystem::create_directories(w.folder, error);
            const auto path = w.folder / ("bail_" + w.session + "_" + std::to_string(fr.bail) + ".csv");
            w.file = _wfsopen(path.c_str(), L"wb", _SH_DENYNO); // readable while the game runs
            if (!w.file) continue;
            w.open_bail = fr.bail;
            write_header(w.file);
            for (const auto& early : w.before) {
                Frame copy = early;
                copy.bail = fr.bail;
                copy.t = -1;
                write_row(w.file, copy);
            }
            w.before.clear();
            logging::log(Level::info, Channel::skater, "Real Bails: recording bail {} to {}", fr.bail, path.string());
        }
        if (w.file) write_row(w.file, fr);
    }
    r.tail.store(tail, std::memory_order_release);
    if (w.file) std::fflush(w.file);
}
}

bool start(std::uintptr_t base) noexcept {
    auto& s = shared();
    if (s.ready.load(std::memory_order_acquire)) return true;
    if (!skater_physics::available() || !no_bail_available()) {
        logging::write(Level::warning, Channel::skater, "Real Bails is unavailable: the skater's physics steps cannot be read.");
        return false;
    }
    try {
        std::array<wchar_t, 32768> module{};
        if (GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())))
            writer().folder = logging::log_directory(std::filesystem::path(module.data()).parent_path()) / L"RealBails";
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &now);
        char stamp[32]{};
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
        writer().session = stamp;
    } catch (...) {}
    (void)start_world(base);
    set_body_motion_callback(&on_physics);
    s.ready.store(true, std::memory_order_release);
    logging::log(Level::info, Channel::skater, "Real Bails ready (muscles {}, recording {}).", s.enabled.load() ? "on" : "off",
                 s.recording.load() ? "on" : "off");
    return true;
}
bool available() noexcept { return shared().ready.load(std::memory_order_acquire); }
// Saved beside the profile (profile_runtime's "ReSkate." store), on unless switched off.
constexpr const char* saved_enabled = "RealBails.Enabled";
constexpr const char* saved_strength = "RealBails.Strength";
constexpr const char* saved_reaction = "RealBails.Reaction";
constexpr const char* saved_head = "RealBails.Head";
constexpr const char* saved_tuck = "RealBails.Tuck";
constexpr const char* saved_lie_still = "RealBails.LieStill";
constexpr const char* saved_grip = "RealBails.Grip";
constexpr const char* saved_catch_arms = "RealBails.CatchArms";
constexpr const char* saved_catch_step = "RealBails.CatchStep";
constexpr const char* saved_grab_hurt = "RealBails.GrabHurt";
constexpr const char* saved_hold_hurt = "RealBails.HoldHurt";
Tuning clamped(Tuning t) noexcept {
    const auto fix = [](float v, float lo, float hi, float otherwise) { return std::isfinite(v) ? std::clamp(v, lo, hi) : otherwise; };
    t.reaction = fix(t.reaction, 0.5f, 2.0f, 1.0f);
    t.head = fix(t.head, 0.0f, 1.5f, 1.0f);
    t.tuck = fix(t.tuck, 0.0f, 1.5f, 1.0f);
    t.lie_still = fix(t.lie_still, 0.5f, 4.0f, 1.6f);
    t.grip = fix(t.grip, 0.0f, 2.0f, 1.0f);
    t.hold_hurt = fix(t.hold_hurt, 1.0f, 10.0f, 4.5f);
    return t;
}
void tick() noexcept {
    if (!available()) return;
    auto& s = shared();
    try {
        if (!s.loaded.load() && profile_runtime::local_profile_store_ready()) {
            if (const auto on = profile_runtime::local_preference(saved_enabled)) s.enabled.store(*on);
            const auto number = [](const char* key, float& into) {
                if (const auto value = profile_runtime::local_value(key); value && value->is_number()) into = static_cast<float>(value->get<double>());
            };
            float strength_now = s.strength.load();
            number(saved_strength, strength_now);
            s.strength.store(std::clamp(strength_now, 0.25f, 2.0f));
            Tuning t;
            number(saved_reaction, t.reaction);
            number(saved_head, t.head);
            number(saved_tuck, t.tuck);
            number(saved_lie_still, t.lie_still);
            number(saved_grip, t.grip);
            number(saved_hold_hurt, t.hold_hurt);
            if (const auto on = profile_runtime::local_preference(saved_catch_arms)) t.catch_arms = *on;
            if (const auto on = profile_runtime::local_preference(saved_catch_step)) t.catch_step = *on;
            if (const auto on = profile_runtime::local_preference(saved_grab_hurt)) t.grab_hurt = *on;
            {
                std::lock_guard lock(s.tuning_mutex);
                s.tuning = clamped(t);
            }
            s.loaded.store(true);
        }
        if (s.loaded.load() && s.dirty.exchange(false)) {
            const auto t = tuning();
            profile_runtime::set_local_preference(saved_enabled, s.enabled.load());
            profile_runtime::set_local_preference(saved_catch_arms, t.catch_arms);
            profile_runtime::set_local_preference(saved_catch_step, t.catch_step);
            profile_runtime::set_local_preference(saved_grab_hurt, t.grab_hurt);
            profile_runtime::set_local_values({{saved_strength, static_cast<double>(s.strength.load())},
                                               {saved_reaction, static_cast<double>(t.reaction)},
                                               {saved_head, static_cast<double>(t.head)},
                                               {saved_tuck, static_cast<double>(t.tuck)},
                                               {saved_lie_still, static_cast<double>(t.lie_still)},
                                               {saved_grip, static_cast<double>(t.grip)},
                                               {saved_hold_hurt, static_cast<double>(t.hold_hurt)}});
        }
    } catch (...) {}
    try { drain(); } catch (...) {}
}
void set_enabled(bool on) noexcept {
    shared().enabled.store(on);
    shared().dirty.store(true);
}
bool enabled() noexcept { return shared().enabled.load(); }
void set_strength(float value) noexcept {
    if (!std::isfinite(value)) return;
    shared().strength.store(std::clamp(value, 0.25f, 2.0f));
    shared().dirty.store(true);
}
float strength() noexcept { return shared().strength.load(); }
Tuning tuning() noexcept {
    auto& s = shared();
    std::lock_guard lock(s.tuning_mutex);
    return s.tuning;
}
void set_tuning(const Tuning& t) noexcept {
    auto& s = shared();
    {
        std::lock_guard lock(s.tuning_mutex);
        s.tuning = clamped(t);
    }
    s.dirty.store(true);
}
void reset_tuning() noexcept {
    set_tuning(Tuning{});
    set_strength(1.0f);
}
void set_recording(bool on) noexcept { shared().recording.store(on); }
bool recording() noexcept { return shared().recording.load(); }
Live live() noexcept {
    auto& s = shared();
    std::lock_guard lock(s.live_mutex);
    return s.live;
}
}
