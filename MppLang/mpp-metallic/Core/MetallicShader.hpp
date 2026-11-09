// Metallic's shading language in C++23: what M++ shaders are compiled to.
//
// `mpp` translates @kernel and @fragment functions (and the structs and
// helper functions they use) into C++ against this header, compiles them
// with MppxxCpp, and runs them from Metallic's engine. Vectors are clang
// vector types, so float4 arithmetic is SIMD and swizzles (.xyz, .rgb, .yx)
// work as in Metal. Anything the translator can't handle keeps running as
// M++.
#pragma once

#include "MetallicCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace mtl {

typedef float float2 __attribute__((ext_vector_type(2)));
typedef float float3 __attribute__((ext_vector_type(3)));
typedef float float4 __attribute__((ext_vector_type(4)));
typedef int32_t int2 __attribute__((ext_vector_type(2)));
typedef int32_t int3 __attribute__((ext_vector_type(3)));
typedef int32_t int4 __attribute__((ext_vector_type(4)));
typedef uint32_t uint2 __attribute__((ext_vector_type(2)));
typedef uint32_t uint3 __attribute__((ext_vector_type(3)));
typedef uint32_t uint4 __attribute__((ext_vector_type(4)));
using half = float;
using half2 = float2;
using half3 = float3;
using half4 = float4;
using uint = uint32_t;

#define MTL_INLINE [[gnu::always_inline]] inline

// MARK: Constructors (float4(rgb, 1), float2(0.5), ...)

MTL_INLINE float2 vec2(float s) { return float2{s, s}; }
MTL_INLINE float2 vec2(float x, float y) { return float2{x, y}; }
MTL_INLINE float2 vec2(float2 v) { return v; }
MTL_INLINE float2 vec2(uint2 v) { return float2{float(v.x), float(v.y)}; }
MTL_INLINE float2 vec2(int2 v) { return float2{float(v.x), float(v.y)}; }
MTL_INLINE float3 vec3(float s) { return float3{s, s, s}; }
MTL_INLINE float3 vec3(float x, float y, float z) { return float3{x, y, z}; }
MTL_INLINE float3 vec3(float2 xy, float z) { return float3{xy.x, xy.y, z}; }
MTL_INLINE float3 vec3(float x, float2 yz) { return float3{x, yz.x, yz.y}; }
MTL_INLINE float3 vec3(float3 v) { return v; }
MTL_INLINE float4 vec4(float s) { return float4{s, s, s, s}; }
MTL_INLINE float4 vec4(float x, float y, float z, float w) { return float4{x, y, z, w}; }
MTL_INLINE float4 vec4(float3 xyz, float w) { return float4{xyz.x, xyz.y, xyz.z, w}; }
MTL_INLINE float4 vec4(float x, float3 yzw) { return float4{x, yzw.x, yzw.y, yzw.z}; }
MTL_INLINE float4 vec4(float2 xy, float z, float w) { return float4{xy.x, xy.y, z, w}; }
MTL_INLINE float4 vec4(float2 xy, float2 zw) { return float4{xy.x, xy.y, zw.x, zw.y}; }
MTL_INLINE float4 vec4(float4 v) { return v; }
MTL_INLINE uint2 uvec2(uint32_t s) { return uint2{s, s}; }
MTL_INLINE uint2 uvec2(uint32_t x, uint32_t y) { return uint2{x, y}; }
MTL_INLINE int2 ivec2(int32_t s) { return int2{s, s}; }
MTL_INLINE int2 ivec2(int32_t x, int32_t y) { return int2{x, y}; }

// MARK: Math with Metal's names

template <class T> concept Vec = requires(T v) { v.x; };

MTL_INLINE float sum(float2 v) { return v.x + v.y; }
MTL_INLINE float sum(float3 v) { return v.x + v.y + v.z; }
MTL_INLINE float sum(float4 v) { return v.x + v.y + v.z + v.w; }
template <Vec V> MTL_INLINE float dot(V a, V b) { return sum(a * b); }
MTL_INLINE float length(float s) { return std::fabs(s); }
template <Vec V> MTL_INLINE float length(V v) { return std::sqrt(dot(v, v)); }
template <Vec V> MTL_INLINE float length_squared(V v) { return dot(v, v); }
template <Vec V> MTL_INLINE float distance(V a, V b) { return length(a - b); }
template <Vec V> MTL_INLINE V normalize(V v) { return v * (1.0f / std::sqrt(dot(v, v))); }
MTL_INLINE float3 cross(float3 a, float3 b) { return float3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

template <class T> concept Scalar = std::is_arithmetic_v<T>;

// Mixed scalars (Int with Float, long with a literal 0) meet in their common type, as in Swift.
template <Scalar A, Scalar B> MTL_INLINE auto min(A a, B b) { using C = std::common_type_t<A, B>; return C(a) < C(b) ? C(a) : C(b); }
template <Scalar A, Scalar B> MTL_INLINE auto max(A a, B b) { using C = std::common_type_t<A, B>; return C(a) > C(b) ? C(a) : C(b); }
template <class A, class B, class C> MTL_INLINE auto min(A a, B b, C c) { return min(min(a, b), c); }
template <class A, class B, class C> MTL_INLINE auto max(A a, B b, C c) { return max(max(a, b), c); }
template <Vec V> MTL_INLINE V min(V a, V b) { return a < b ? a : b; }
template <Vec V> MTL_INLINE V max(V a, V b) { return a > b ? a : b; }
template <Vec V, Scalar S> MTL_INLINE V min(V a, S b) { return min(a, V(float(b))); }
template <Vec V, Scalar S> MTL_INLINE V max(V a, S b) { return max(a, V(float(b))); }
template <Vec V, Scalar S> MTL_INLINE V min(S a, V b) { return min(V(float(a)), b); }
template <Vec V, Scalar S> MTL_INLINE V max(S a, V b) { return max(V(float(a)), b); }

template <Scalar A, Scalar B, Scalar C> MTL_INLINE auto clamp(A x, B lo, C hi) { return min(max(x, lo), hi); }
template <Vec V, Scalar A, Scalar B> MTL_INLINE V clamp(V x, A lo, B hi) { return min(max(x, lo), hi); }
template <Vec V> MTL_INLINE V clamp(V x, V lo, V hi) { return min(max(x, lo), hi); }
MTL_INLINE float saturate(float x) { return clamp(x, 0.0f, 1.0f); }
template <Vec V> MTL_INLINE V saturate(V x) { return clamp(x, 0.0f, 1.0f); }
template <Scalar A, Scalar B, Scalar T> MTL_INLINE float mix(A a, B b, T t) { return float(a) + (float(b) - float(a)) * float(t); }
template <Vec V, Scalar T> MTL_INLINE V mix(V a, V b, T t) { return a + (b - a) * float(t); }
template <Vec V> MTL_INLINE V mix(V a, V b, V t) { return a + (b - a) * t; }
MTL_INLINE float step(float edge, float x) { return x < edge ? 0.0f : 1.0f; }
template <Scalar A, Scalar B, Scalar X> MTL_INLINE float smoothstep(A e0_, B e1_, X x_) {
    const float e0 = float(e0_), e1 = float(e1_), x = float(x_);
    const float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
template <Vec V, Scalar A, Scalar B> MTL_INLINE V smoothstep(A e0_, B e1_, V x) {
    const float e0 = float(e0_), e1 = float(e1_);
    const V t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

template <Scalar T> MTL_INLINE T abs(T x) { return x < 0 ? -x : x; }
template <Vec V> MTL_INLINE V abs(V v) { return v < 0 ? -v : v; }
MTL_INLINE float sign(float x) { return x > 0 ? 1.0f : (x < 0 ? -1.0f : 0.0f); }
MTL_INLINE float fract(float x) { return x - std::floor(x); }
MTL_INLINE float radians(float d) { return d * 0.017453292519943295f; }
MTL_INLINE float degrees(float r) { return r * 57.29577951308232f; }
MTL_INLINE float rsqrt(float x) { return 1.0f / std::sqrt(x); }

/// sin and cos as Metal computes them by default (fast math): Cody–Waite
/// reduction by π/2 and minimax polynomials, within a few ulp for
/// |x| < 8192 (larger arguments use libm). Inlined, so loops stay vectorizable.
MTL_INLINE void sinCosReduced(float x, float& s, float& c) {
    // nearest multiple of π/2 (adding 1.5·2²³ rounds to an integer; |x| < 8192)
    const float j = (x * 0.636619772367581f + 12582912.0f) - 12582912.0f;
    const int q = int(j);
    float r = x - j * 1.5703125f;
    r = r - j * 4.837512969970703125e-4f;
    r = r - j * 7.549789948768648e-8f;
    const float r2 = r * r;
    const float ps = r + r * r2 * (-1.6666654611e-1f + r2 * (8.3321608736e-3f + r2 * -1.9515295891e-4f));
    const float pc = 1.0f - 0.5f * r2 + r2 * r2 * (4.166664568298827e-2f + r2 * (-1.388731625493765e-3f + r2 * 2.443315711809948e-5f));
    switch (q & 3) {
    case 0: s = ps; c = pc; break;
    case 1: s = pc; c = -ps; break;
    case 2: s = -ps; c = -pc; break;
    default: s = -pc; c = ps; break;
    }
}
MTL_INLINE float fastSin(float x) {
    if (!(std::fabs(x) < 8192.0f)) return std::sin(x);
    float s, c;
    sinCosReduced(x, s, c);
    return s;
}
MTL_INLINE float fastCos(float x) {
    if (!(std::fabs(x) < 8192.0f)) return std::cos(x);
    float s, c;
    sinCosReduced(x, s, c);
    return c;
}
MTL_INLINE float fastTan(float x) {
    if (!(std::fabs(x) < 8192.0f)) return std::tan(x);
    float s, c;
    sinCosReduced(x, s, c);
    return s / c;
}

#define MTL_UNARY(name, fn)                                                                                     \
    MTL_INLINE float name(float x) { return fn(x); }                                                          \
    MTL_INLINE float2 name(float2 v) { return float2{fn(v.x), fn(v.y)}; }                                     \
    MTL_INLINE float3 name(float3 v) { return float3{fn(v.x), fn(v.y), fn(v.z)}; }                             \
    MTL_INLINE float4 name(float4 v) { return float4{fn(v.x), fn(v.y), fn(v.z), fn(v.w)}; }
MTL_UNARY(sqrt, std::sqrt)
MTL_UNARY(sin, fastSin)
MTL_UNARY(cos, fastCos)
MTL_UNARY(tan, fastTan)
MTL_UNARY(asin, std::asin)
MTL_UNARY(acos, std::acos)
MTL_UNARY(atan, std::atan)
MTL_UNARY(exp, std::exp)
MTL_UNARY(exp2, std::exp2)
MTL_UNARY(log, std::log)
MTL_UNARY(log2, std::log2)
MTL_UNARY(floor, std::floor)
MTL_UNARY(ceil, std::ceil)
MTL_UNARY(round, std::round)
MTL_UNARY(trunc, std::trunc)
#undef MTL_UNARY
template <Scalar A, Scalar B> MTL_INLINE float pow(A x, B y) { return std::pow(float(x), float(y)); }
template <Vec V> MTL_INLINE V pow(V x, float y) {
    V r = x;
    for (int i = 0; i < int(sizeof(V) / sizeof(float)); ++i) r[i] = std::pow(x[i], y);
    return r;
}
template <Scalar A, Scalar B> MTL_INLINE float atan2(A y, B x) { return std::atan2(float(y), float(x)); }
template <Scalar A, Scalar B> MTL_INLINE float fmod(A x, B y) { return std::fmod(float(x), float(y)); }
MTL_INLINE bool isfinite(float x) { return std::isfinite(x); }
inline constexpr float pi = 3.14159265358979323846f;

// MARK: Samplers and textures

/// floor for finite values within ±1e8 (what sampling needs), without a libm call.
MTL_INLINE float fastFloor(float x) {
#if defined(__SSE4_1__) || defined(__aarch64__)
    return __builtin_floorf(x);
#else
    const float t = float(long(x));
    return t > x ? t - 1.0f : t;
#endif
}

struct sampler {
    int filter = 1, address = 0;
    sampler() = default;
    sampler(const mtl_sampler& s) : filter(s.filter), address(s.address) {}
};

struct texture2d {
    float4* texels = nullptr;
    long width = 0, height = 0;
    texture2d() = default;
    texture2d(const mtl_texture& t) : texels(reinterpret_cast<float4*>(t.texels)), width(t.width), height(t.height) {}

    MTL_INLINE uint32_t get_width() const { return uint32_t(width); }
    MTL_INLINE uint32_t get_height() const { return uint32_t(height); }

    MTL_INLINE float4 read(uint2 c) const {
        const long x = c.x < width ? long(c.x) : width - 1, y = c.y < height ? long(c.y) : height - 1;
        return texels[y * width + x];
    }
    MTL_INLINE float4 read(long x, long y) const {
        return texels[std::clamp(y, 0L, height - 1) * width + std::clamp(x, 0L, width - 1)];
    }
    MTL_INLINE void write(float4 v, uint2 c) const {
        if (long(c.x) < width && long(c.y) < height) texels[long(c.y) * width + long(c.x)] = v;
    }

    MTL_INLINE long wrap(long i, long n, int address) const {
        switch (address) {
        case 1: { const long m = i % n; return m < 0 ? m + n : m; }
        case 2: { const long p = 2 * n, m0 = i % p, m = m0 < 0 ? m0 + p : m0; return m < n ? m : p - 1 - m; }
        case 3: return i < 0 || i >= n ? -1 : i;
        default: return std::clamp(i, 0L, n - 1);
        }
    }
    MTL_INLINE float4 texel(long x, long y, int address) const {
        const long wx = wrap(x, width, address), wy = wrap(y, height, address);
        return wx < 0 || wy < 0 ? float4(0) : texels[wy * width + wx];
    }

    /// Samples at normalized coordinates (0...1, origin top-left).
    MTL_INLINE float4 sample(sampler s, float2 uv) const {
        float fx = uv.x * float(width) - 0.5f, fy = uv.y * float(height) - 0.5f;
        if (!(std::isfinite(fx) && std::isfinite(fy))) return float4(0);
        fx = std::clamp(fx, -1e8f, 1e8f);
        fy = std::clamp(fy, -1e8f, 1e8f);
        if (s.filter == 0) return texel(long(fastFloor(fx + 0.5f)), long(fastFloor(fy + 0.5f)), s.address);
        const float x0f = fastFloor(fx), y0f = fastFloor(fy);
        const float tx = fx - x0f, ty = fy - y0f;
        const long x0 = long(x0f), y0 = long(y0f);
        float4 a, b, c, d;
        if (s.address == 0) {
            const long xa = std::clamp(x0, 0L, width - 1), xb = std::clamp(x0 + 1, 0L, width - 1);
            const float4* r0 = texels + std::clamp(y0, 0L, height - 1) * width;
            const float4* r1 = texels + std::clamp(y0 + 1, 0L, height - 1) * width;
            a = r0[xa]; b = r0[xb]; c = r1[xa]; d = r1[xb];
        } else {
            a = texel(x0, y0, s.address); b = texel(x0 + 1, y0, s.address);
            c = texel(x0, y0 + 1, s.address); d = texel(x0 + 1, y0 + 1, s.address);
        }
        const float4 top = a + (b - a) * tx, bottom = c + (d - c) * tx;
        return top + (bottom - top) * ty;
    }
};

/// [[position]] and the other built-ins.
struct thread {
    uint2 threadPosition{0, 0}, threadsPerGrid{0, 0};
    uint32_t vertexID = 0, instanceID = 0;
    float4 position{0, 0, 0, 0};
};

// MARK: The loops the engine calls

/// Denormals flush to zero while a shader runs, as on a GPU (Metal does the
/// same). Besides matching the GPU, it keeps the CPU off its slow path for
/// tiny values — including the unused fourth lane of a float3.
class FlushDenormals {
public:
#if defined(__x86_64__) || defined(__i386__)
    FlushDenormals() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x8040); }   // FTZ | DAZ
    ~FlushDenormals() { _mm_setcsr(saved_); }
private:
    unsigned saved_;
#elif defined(__aarch64__)
    FlushDenormals() {
        __asm__ volatile("mrs %0, fpcr" : "=r"(saved_));
        const unsigned long v = saved_ | (1ul << 24);                               // FZ
        __asm__ volatile("msr fpcr, %0" ::"r"(v));
    }
    ~FlushDenormals() { __asm__ volatile("msr fpcr, %0" ::"r"(saved_)); }
private:
    unsigned long saved_;
#else
    FlushDenormals() {}
#endif
    FlushDenormals(const FlushDenormals&) = delete;
    FlushDenormals& operator=(const FlushDenormals&) = delete;
};

template <class Kernel>
MTL_INLINE void runRows(const mtl_rows& r, Kernel&& k) {
    const FlushDenormals flush;
    thread t;
    t.threadsPerGrid = uint2{uint32_t(r.width), uint32_t(r.height)};
    for (long y = r.y0; y < r.y1; ++y)
        for (long x = 0; x < r.width; ++x) {
            t.threadPosition = uint2{uint32_t(x), uint32_t(y)};
            k(t);
        }
}

struct NoVaryings {
    static NoVaryings interpolate(const NoVaryings& a, const NoVaryings&, const NoVaryings&, float3) { return a; }
};

/// Runs a fragment shader over one row of a triangle (perspective-correct
/// varyings; source-over blending when the pipeline asks for it).
template <class V, class Fragment>
MTL_INLINE void shadeSpan(const mtl_span& s, Fragment&& f) {
    const FlushDenormals flush;
    const float ax = s.a[0], ay = s.a[1], az = s.a[2], bx = s.b[0], by = s.b[1], bz = s.b[2], cx = s.c[0], cy = s.c[1], cz = s.c[2];
    const float iwa = s.invW[0], iwb = s.invW[1], iwc = s.invW[2];
    const V va = s.va ? *static_cast<const V*>(s.va) : V{}, vb = s.vb ? *static_cast<const V*>(s.vb) : V{},
            vc = s.vc ? *static_cast<const V*>(s.vc) : V{};
    float4* row = reinterpret_cast<float4*>(s.row);
    const float fy = float(s.y) + 0.5f;
    thread t;
    for (long px = s.x0; px < s.x1; ++px) {
        const float fx = float(px) + 0.5f;
        const float w0 = ((bx - fx) * (cy - fy) - (by - fy) * (cx - fx)) * s.inv;
        const float w1 = ((cx - fx) * (ay - fy) - (cy - fy) * (ax - fx)) * s.inv;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const float p0 = w0 * iwa, p1 = w1 * iwb, p2 = w2 * iwc;
        const float sw = p0 + p1 + p2, inv = 1.0f / sw;
        t.position = float4{fx, fy, w0 * az + w1 * bz + w2 * cz, sw};
        const float4 color = f(t, V::interpolate(va, vb, vc, float3{p0 * inv, p1 * inv, p2 * inv}));
        float4& d = row[px];
        if (s.blending) {
            const float sa = saturate(color.w);
            d = color * sa + d * (1 - sa);
            d.w = sa + (d.w - color.w * sa);
        } else {
            d = color;
        }
    }
}

}  // namespace mtl
