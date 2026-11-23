// Pantomimus - small 3D maths: vectors, quaternions and 4x4 matrices (column major, as OpenGL and glTF).
#pragma once
#include <algorithm>
#include <cmath>

namespace M {

constexpr float PI = 3.14159265358979f;

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    constexpr V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator/(float s) const { return {x / s, y / s, z / s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const V3& a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(const V3& a, const V3& fallback = V3(0, 1, 0)) { const float l = length(a); return l > 1e-8f ? a / l : fallback; }
inline V3 lerp(const V3& a, const V3& b, float t) { return a + (b - a) * t; }
// a without its part along the unit vector n
inline V3 reject(const V3& a, const V3& n) { return a - n * dot(a, n); }

struct Q {   // w + xi + yj + zk, unit length for rotations
    float w = 1, x = 0, y = 0, z = 0;
    Q() = default;
    constexpr Q(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}
    Q operator*(const Q& o) const {
        return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
    }
};
inline Q conj(const Q& q) { return {q.w, -q.x, -q.y, -q.z}; }
inline float dot(const Q& a, const Q& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
inline Q normalize(const Q& q) {
    const float l = std::sqrt(dot(q, q));
    return l > 1e-12f ? Q(q.w / l, q.x / l, q.y / l, q.z / l) : Q();
}
inline V3 rotate(const Q& q, const V3& v) {
    const V3 u(q.x, q.y, q.z);
    const V3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
inline Q axisAngle(const V3& axis, float angle) {
    const V3 a = normalize(axis);
    const float s = std::sin(angle * 0.5f);
    return {std::cos(angle * 0.5f), a.x * s, a.y * s, a.z * s};
}
// the shortest turn taking direction a to direction b (unit vectors)
inline Q fromTo(const V3& a, const V3& b) {
    const float d = dot(a, b);
    if (d < -0.999999f) {
        V3 axis = cross(V3(1, 0, 0), a);
        if (length(axis) < 1e-4f) axis = cross(V3(0, 1, 0), a);
        return axisAngle(axis, PI);
    }
    const V3 c = cross(a, b);
    return normalize(Q(1 + d, c.x, c.y, c.z));
}
inline Q slerp(Q a, const Q& b, float t) {
    float d = dot(a, b);
    if (d < 0) { a = Q(-a.w, -a.x, -a.y, -a.z); d = -d; }
    if (d > 0.9995f) return normalize(Q(a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t));
    const float th = std::acos(std::min(1.0f, d)), s = std::sin(th);
    const float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb};
}
// the rotation whose columns are the orthonormal axes x, y, z
inline Q fromAxes(const V3& x, const V3& y, const V3& z) {
    const float m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    const float tr = m00 + m11 + m22;
    Q q;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1) * 2;
        q = {0.25f * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1 + m00 - m11 - m22) * 2;
        q = {(m21 - m12) / s, 0.25f * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1 + m11 - m00 - m22) * 2;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25f * s, (m12 + m21) / s};
    } else {
        const float s = std::sqrt(1 + m22 - m00 - m11) * 2;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25f * s};
    }
    return normalize(q);
}
// an orthonormal frame with y along the main direction and z as near the hint as y allows
inline Q frameYZ(const V3& mainY, const V3& hintZ) {
    const V3 y = normalize(mainY);
    V3 z = reject(hintZ, y);
    if (length(z) < 1e-6f) z = reject(std::fabs(y.z) < 0.9f ? V3(0, 0, 1) : V3(1, 0, 0), y);
    z = normalize(z);
    return fromAxes(cross(y, z), y, z);
}
// a frame with x along the main direction and y as near the hint as x allows
inline Q frameXY(const V3& mainX, const V3& hintY) {
    const V3 x = normalize(mainX);
    V3 y = reject(hintY, x);
    if (length(y) < 1e-6f) y = reject(std::fabs(x.y) < 0.9f ? V3(0, 1, 0) : V3(0, 0, 1), x);
    y = normalize(y);
    return fromAxes(x, y, cross(x, y));
}
// the turn about the unit axis n hidden in q (its twist), q = swing * twist
inline Q twistAbout(const Q& q, const V3& n) {
    const V3 r(q.x, q.y, q.z);
    const V3 p = n * dot(r, n);
    Q t(q.w, p.x, p.y, p.z);
    const float l = std::sqrt(dot(t, t));
    return l < 1e-9f ? Q() : Q(t.w / l, t.x / l, t.y / l, t.z / l);
}
inline float angleOf(const Q& q) { return 2 * std::acos(std::min(1.0f, std::fabs(q.w))); }
// Euler angles (radians): R = Rz * Ry * Rx (FBX's and glTF tools' "XYZ")
inline V3 toEulerXYZ(const Q& q) {
    const float sinp = 2 * (q.w * q.y - q.z * q.x);
    V3 e;
    e.x = std::atan2(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y));
    e.y = std::fabs(sinp) >= 1 ? std::copysign(PI / 2, sinp) : std::asin(sinp);
    e.z = std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z));
    return e;
}
inline Q fromEulerXYZ(const V3& e) { return axisAngle({0, 0, 1}, e.z) * axisAngle({0, 1, 0}, e.y) * axisAngle({1, 0, 0}, e.x); }
// BVH's usual "Zrotation Xrotation Yrotation": R = Rz * Rx * Ry
inline V3 toEulerZXY(const Q& q) {
    // matrix entries of R
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    const float m21 = 2 * (yz + wx), m20 = 2 * (xz - wy), m22 = 1 - 2 * (xx + yy), m01 = 2 * (xy - wz), m11 = 1 - 2 * (xx + zz);
    V3 e;   // Rz*Rx*Ry: m21 = sin(x)
    e.x = std::asin(std::clamp(m21, -1.0f, 1.0f));
    if (std::fabs(m21) < 0.99999f) {
        e.y = std::atan2(-m20, m22);
        e.z = std::atan2(-m01, m11);
    } else {   // gimbal lock: put it all in z
        const float m10 = 2 * (xy + wz), m00 = 1 - 2 * (yy + zz);
        e.y = 0; e.z = std::atan2(m10, m00);
    }
    return e;
}
inline Q fromEulerZXY(const V3& e) { return axisAngle({0, 0, 1}, e.z) * axisAngle({1, 0, 0}, e.x) * axisAngle({0, 1, 0}, e.y); }
// keeps a quaternion on the same side as the one before it (for smooth curves)
inline Q sameHemisphere(const Q& q, const Q& prev) { return dot(q, prev) < 0 ? Q(-q.w, -q.x, -q.y, -q.z) : q; }

struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& operator()(int r, int c) { return m[c * 4 + r]; }
    float operator()(int r, int c) const { return m[c * 4 + r]; }
    M4 operator*(const M4& o) const {
        M4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += (*this)(i, k) * o(k, j);
                r(i, j) = s;
            }
        return r;
    }
};
inline M4 trs(const V3& t, const Q& q, float s = 1) {
    M4 r;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r(0, 0) = (1 - 2 * (yy + zz)) * s; r(0, 1) = 2 * (xy - wz) * s; r(0, 2) = 2 * (xz + wy) * s;
    r(1, 0) = 2 * (xy + wz) * s; r(1, 1) = (1 - 2 * (xx + zz)) * s; r(1, 2) = 2 * (yz - wx) * s;
    r(2, 0) = 2 * (xz - wy) * s; r(2, 1) = 2 * (yz + wx) * s; r(2, 2) = (1 - 2 * (xx + yy)) * s;
    r(0, 3) = t.x; r(1, 3) = t.y; r(2, 3) = t.z;
    return r;
}
inline M4 perspective(float fovy, float aspect, float zn, float zf) {
    M4 r; const float f = 1 / std::tan(fovy / 2);
    r(0, 0) = f / aspect; r(1, 1) = f; r(2, 2) = (zf + zn) / (zn - zf); r(2, 3) = 2 * zf * zn / (zn - zf); r(3, 2) = -1; r(3, 3) = 0;
    return r;
}
inline M4 lookAt(const V3& eye, const V3& at, const V3& up) {
    const V3 f = normalize(at - eye), s = normalize(cross(f, up)), u = cross(s, f);
    M4 r;
    r(0, 0) = s.x; r(0, 1) = s.y; r(0, 2) = s.z; r(0, 3) = -dot(s, eye);
    r(1, 0) = u.x; r(1, 1) = u.y; r(1, 2) = u.z; r(1, 3) = -dot(u, eye);
    r(2, 0) = -f.x; r(2, 1) = -f.y; r(2, 2) = -f.z; r(2, 3) = dot(f, eye);
    return r;
}
inline V3 transform(const M4& a, const V3& p, float* wOut = nullptr) {
    const float w = a(3, 0) * p.x + a(3, 1) * p.y + a(3, 2) * p.z + a(3, 3);
    if (wOut) *wOut = w;
    return {a(0, 0) * p.x + a(0, 1) * p.y + a(0, 2) * p.z + a(0, 3), a(1, 0) * p.x + a(1, 1) * p.y + a(1, 2) * p.z + a(1, 3),
            a(2, 0) * p.x + a(2, 1) * p.y + a(2, 2) * p.z + a(2, 3)};
}

}  // namespace M
