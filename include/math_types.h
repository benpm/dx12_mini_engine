#pragma once

#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

// ---------------------------------------------------------------------------
// vec2
// ---------------------------------------------------------------------------
struct vec2 : XMFLOAT2
{
    vec2() : XMFLOAT2(0, 0) {}
    vec2(float x, float y) : XMFLOAT2(x, y) {}
    explicit vec2(float s) : XMFLOAT2(s, s) {}
    vec2(const XMFLOAT2& v) : XMFLOAT2(v) {}

    vec2 operator+(const vec2& r) const { return { x + r.x, y + r.y }; }
    vec2 operator-(const vec2& r) const { return { x - r.x, y - r.y }; }
    vec2 operator*(float s) const { return { x * s, y * s }; }
    vec2 operator/(float s) const { return { x / s, y / s }; }
    vec2& operator+=(const vec2& r)
    {
        x += r.x;
        y += r.y;
        return *this;
    }
    vec2& operator-=(const vec2& r)
    {
        x -= r.x;
        y -= r.y;
        return *this;
    }
};

// ---------------------------------------------------------------------------
// vec3
// ---------------------------------------------------------------------------
struct vec3 : XMFLOAT3
{
    vec3() : XMFLOAT3(0, 0, 0) {}
    vec3(float x, float y, float z) : XMFLOAT3(x, y, z) {}
    explicit vec3(float s) : XMFLOAT3(s, s, s) {}
    vec3(const XMFLOAT3& v) : XMFLOAT3(v) {}

    vec3 operator+(const vec3& r) const { return { x + r.x, y + r.y, z + r.z }; }
    vec3 operator-(const vec3& r) const { return { x - r.x, y - r.y, z - r.z }; }
    vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    vec3 operator/(float s) const { return { x / s, y / s, z / s }; }
    vec3 operator-() const { return { -x, -y, -z }; }
    vec3& operator+=(const vec3& r)
    {
        x += r.x;
        y += r.y;
        z += r.z;
        return *this;
    }
    vec3& operator-=(const vec3& r)
    {
        x -= r.x;
        y -= r.y;
        z -= r.z;
        return *this;
    }
    vec3& operator*=(float s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
};

// ---------------------------------------------------------------------------
// vec4
// ---------------------------------------------------------------------------
struct vec4 : XMFLOAT4
{
    vec4() : XMFLOAT4(0, 0, 0, 0) {}
    vec4(float x, float y, float z, float w) : XMFLOAT4(x, y, z, w) {}
    explicit vec4(float s) : XMFLOAT4(s, s, s, s) {}
    vec4(const vec3& v, float w) : XMFLOAT4(v.x, v.y, v.z, w) {}
    vec4(const XMFLOAT4& v) : XMFLOAT4(v) {}

    vec3 xyz() const { return { x, y, z }; }

    vec4 operator+(const vec4& r) const { return { x + r.x, y + r.y, z + r.z, w + r.w }; }
    vec4 operator-(const vec4& r) const { return { x - r.x, y - r.y, z - r.z, w - r.w }; }
    vec4 operator*(float s) const { return { x * s, y * s, z * s, w * s }; }
};

// ---------------------------------------------------------------------------
// mat4 — 16-byte aligned, binary-compatible with XMMATRIX for GPU upload
// ---------------------------------------------------------------------------
struct alignas(16) mat4 : XMFLOAT4X4
{
    mat4() : XMFLOAT4X4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) {}

    mat4(
        float m00,
        float m01,
        float m02,
        float m03,
        float m10,
        float m11,
        float m12,
        float m13,
        float m20,
        float m21,
        float m22,
        float m23,
        float m30,
        float m31,
        float m32,
        float m33
    )
        : XMFLOAT4X4(m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33)
    {
    }

    mat4(const XMFLOAT4X4& m) : XMFLOAT4X4(m) {}
    mat4(const XMMATRIX& m) { XMStoreFloat4x4(this, m); }

    XMMATRIX load() const { return XMLoadFloat4x4(this); }
    operator XMMATRIX() const { return load(); }

    mat4 operator*(const mat4& rhs) const { return mat4(load() * rhs.load()); }
    mat4& operator*=(const mat4& rhs)
    {
        *this = *this * rhs;
        return *this;
    }

    static mat4 identity() { return {}; }
};

// ---------------------------------------------------------------------------
// Matrix construction functions
// ---------------------------------------------------------------------------
inline mat4 perspective(float fovY, float aspectRatio, float nearZ, float farZ)
{
    return mat4(XMMatrixPerspectiveFovLH(fovY, aspectRatio, nearZ, farZ));
}

inline mat4 lookAt(const vec3& eye, const vec3& target, const vec3& up)
{
    return mat4(XMMatrixLookAtLH(
        XMVectorSet(eye.x, eye.y, eye.z, 0), XMVectorSet(target.x, target.y, target.z, 0),
        XMVectorSet(up.x, up.y, up.z, 0)
    ));
}

inline mat4 scale(float sx, float sy, float sz)
{
    return mat4(XMMatrixScaling(sx, sy, sz));
}

inline mat4 scale(float s)
{
    return scale(s, s, s);
}

inline mat4 translate(float tx, float ty, float tz)
{
    return mat4(XMMatrixTranslation(tx, ty, tz));
}

inline mat4 translate(const vec3& t)
{
    return translate(t.x, t.y, t.z);
}

inline mat4 rotateQuaternion(float qx, float qy, float qz, float qw)
{
    return mat4(XMMatrixRotationQuaternion(XMVectorSet(qx, qy, qz, qw)));
}

inline mat4 rotateAxis(const vec3& axis, float angle)
{
    return mat4(XMMatrixRotationAxis(XMVectorSet(axis.x, axis.y, axis.z, 0), angle));
}

// ---------------------------------------------------------------------------
// Vector functions
// ---------------------------------------------------------------------------
inline float dot(const vec3& a, const vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float length(const vec3& v)
{
    return std::sqrt(dot(v, v));
}

inline vec3 normalize(const vec3& v)
{
    float len = length(v);
    return len > 0.0f ? vec3(v.x / len, v.y / len, v.z / len) : v;
}

inline vec3 cross(const vec3& a, const vec3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline float dot(const vec2& a, const vec2& b)
{
    return a.x * b.x + a.y * b.y;
}

inline float length(const vec2& v)
{
    return std::sqrt(dot(v, v));
}
