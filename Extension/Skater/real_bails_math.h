#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Small vector and rotation helpers for Real Bails (real_bails_model.h). Plain C++, testable anywhere.
// Rotations are kept as 3x3 matrices whose columns are a body's local axes in the world (a body's Frostbite
// transform has them as rows: right, up, forward), and as rotation vectors (axis times angle) for errors and spins.
namespace dingosdk::real_bails::math {
using V3 = std::array<float, 3>;
using M3 = std::array<V3, 3>; // columns

constexpr V3 operator+(const V3& a, const V3& b) noexcept { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
constexpr V3 operator-(const V3& a, const V3& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
constexpr V3 operator-(const V3& a) noexcept { return {-a[0], -a[1], -a[2]}; }
constexpr V3 operator*(const V3& a, float s) noexcept { return {a[0] * s, a[1] * s, a[2] * s}; }
constexpr V3 operator*(float s, const V3& a) noexcept { return a * s; }
constexpr float dot(const V3& a, const V3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
constexpr V3 cross(const V3& a, const V3& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline float length(const V3& a) noexcept { return std::sqrt(dot(a, a)); }
inline V3 normalized(const V3& a, const V3& fallback = {0, 1, 0}) noexcept {
    const float n = length(a);
    return n > 1e-6f && std::isfinite(n) ? a * (1.0f / n) : fallback;
}
inline bool finite(const V3& a) noexcept { return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]); }
inline V3 clamp_length(const V3& a, float most) noexcept {
    const float n = length(a);
    return n > most && n > 0 ? a * (most / n) : a;
}
// The part of `a` across `axis` (unit).
constexpr V3 across(const V3& a, const V3& axis) noexcept { return a - axis * dot(a, axis); }

constexpr V3 turn_by(const M3& m, const V3& v) noexcept { return m[0] * v[0] + m[1] * v[1] + m[2] * v[2]; }
// m^T v: a world vector in the body's own axes.
constexpr V3 turn_back(const M3& m, const V3& v) noexcept { return {dot(m[0], v), dot(m[1], v), dot(m[2], v)}; }
constexpr M3 multiply(const M3& a, const M3& b) noexcept { return {turn_by(a, b[0]), turn_by(a, b[1]), turn_by(a, b[2])}; }
constexpr M3 transposed(const M3& m) noexcept {
    return {V3{m[0][0], m[1][0], m[2][0]}, V3{m[0][1], m[1][1], m[2][1]}, V3{m[0][2], m[1][2], m[2][2]}};
}
// The rotation by `angle` about `axis` (unit).
inline M3 rotation(const V3& axis, float angle) noexcept {
    const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    const float x = axis[0], y = axis[1], z = axis[2];
    return {V3{t * x * x + c, t * x * y + s * z, t * x * z - s * y}, V3{t * x * y - s * z, t * y * y + c, t * y * z + s * x},
            V3{t * x * z + s * y, t * y * z - s * x, t * z * z + c}};
}
inline V3 turned(const V3& v, const V3& axis, float angle) noexcept { return turn_by(rotation(axis, angle), v); }
// The rotation vector (axis x angle, angle in [0, pi]) of a rotation matrix.
inline V3 rotation_vector(const M3& m) noexcept {
    // Through a quaternion, which holds up near 0 and pi.
    const float trace = m[0][0] + m[1][1] + m[2][2];
    float w, x, y, z;
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2;
        w = 0.25f * s;
        x = (m[1][2] - m[2][1]) / s;
        y = (m[2][0] - m[0][2]) / s;
        z = (m[0][1] - m[1][0]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2;
        w = (m[1][2] - m[2][1]) / s;
        x = 0.25f * s;
        y = (m[1][0] + m[0][1]) / s;
        z = (m[2][0] + m[0][2]) / s;
    } else if (m[1][1] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2;
        w = (m[2][0] - m[0][2]) / s;
        x = (m[1][0] + m[0][1]) / s;
        y = 0.25f * s;
        z = (m[2][1] + m[1][2]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2;
        w = (m[0][1] - m[1][0]) / s;
        x = (m[2][0] + m[0][2]) / s;
        y = (m[2][1] + m[1][2]) / s;
        z = 0.25f * s;
    }
    if (w < 0) { w = -w; x = -x; y = -y; z = -z; }
    const float sin_half = std::sqrt(x * x + y * y + z * z);
    if (sin_half < 1e-7f) return {0, 0, 0};
    const float angle = 2 * std::atan2(sin_half, w);
    return V3{x, y, z} * (angle / sin_half);
}
// The rotation vector that turns direction `from` onto `to` (both unit), the shortest way.
inline V3 swing_between(const V3& from, const V3& to) noexcept {
    const V3 axis = cross(from, to);
    const float s = length(axis), c = std::clamp(dot(from, to), -1.0f, 1.0f);
    if (s < 1e-6f) {
        if (c > 0) return {0, 0, 0};
        // Opposite: any axis across `from`.
        const V3 other = std::abs(from[0]) < 0.9f ? V3{1, 0, 0} : V3{0, 1, 0};
        return normalized(cross(from, other)) * 3.14159265f;
    }
    return axis * (std::atan2(s, c) / s);
}
// A body's axes (columns) from its Frostbite rows, made orthonormal.
inline M3 from_rows(const V3& right, const V3& up, const V3& forward) noexcept {
    const V3 x = normalized(right, {1, 0, 0});
    const V3 y = normalized(across(up, x), {0, 1, 0});
    V3 z = cross(x, y);
    if (dot(z, forward) < 0) z = -z; // keep the game's own handedness
    return {x, y, z};
}
// 3x3 matrices as sums and products, for inertias (symmetric) and the small linear solves of the body's mechanics.
constexpr M3 identity3() noexcept { return {V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1}}; }
constexpr M3 add(const M3& a, const M3& b) noexcept { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
constexpr M3 subtract(const M3& a, const M3& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
constexpr M3 scaled(const M3& a, float s) noexcept { return {a[0] * s, a[1] * s, a[2] * s}; }
// u v^T.
constexpr M3 outer(const V3& u, const V3& v) noexcept { return {u * v[0], u * v[1], u * v[2]}; }
// [a]x: [a]x v = a x v.
constexpr M3 skew(const V3& a) noexcept { return {V3{0, a[2], -a[1]}, V3{-a[2], 0, a[0]}, V3{a[1], -a[0], 0}}; }
// The inertia of a point mass `m` at `r` from a centre: m (|r|^2 E - r r^T).
constexpr M3 point_inertia(float m, const V3& r) noexcept {
    return scaled(subtract(scaled(identity3(), dot(r, r)), outer(r, r)), m);
}
inline bool finite(const M3& m) noexcept { return finite(m[0]) && finite(m[1]) && finite(m[2]); }
// The inverse; a matrix that is (nearly) singular comes back as the inverse of its mean diagonal, so nothing blows up.
inline M3 inverse(const M3& m) noexcept {
    const float a = m[0][0], b = m[1][0], c = m[2][0];
    const float d = m[0][1], e = m[1][1], f = m[2][1];
    const float g = m[0][2], h = m[1][2], i = m[2][2];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const float scale = std::abs(a) + std::abs(e) + std::abs(i);
    if (!std::isfinite(det) || std::abs(det) <= 1e-9f * scale * scale * scale || scale <= 0) {
        const float mean = scale / 3;
        return scaled(identity3(), mean > 1e-9f ? 1.0f / mean : 0.0f);
    }
    const float k = 1.0f / det;
    return {V3{(e * i - f * h) * k, (f * g - d * i) * k, (d * h - e * g) * k}, V3{(c * h - b * i) * k, (a * i - c * g) * k, (b * g - a * h) * k},
            V3{(b * f - c * e) * k, (c * d - a * f) * k, (a * e - b * d) * k}};
}
// The rotation of a rotation vector (axis x angle).
inline M3 rotation(const V3& turn) noexcept {
    const float angle = length(turn);
    return angle > 1e-7f ? rotation(turn * (1.0f / angle), angle) : identity3();
}
// Two-bone reach: where the middle joint goes for `root` to reach `target` with bones of lengths a and b, bent
// towards `pole`. The target is pulled in when out of reach.
inline V3 reach_middle(const V3& root, const V3& target, float a, float b, const V3& pole) noexcept {
    V3 to = target - root;
    float d = length(to);
    const V3 dir = normalized(to, {0, -1, 0});
    d = std::clamp(d, std::abs(a - b) + 0.01f, (a + b) * 0.999f);
    const float along = (a * a - b * b + d * d) / (2 * d);
    const float up = std::sqrt(std::max(0.0f, a * a - along * along));
    const V3 side = normalized(across(pole, dir), normalized(cross(dir, {0, 1, 0}), {1, 0, 0}));
    return root + dir * along + side * up;
}
}
