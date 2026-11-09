// Metallic's core, in C++23 (MetallicCore.cpp), built by MppxxCpp with its
// usual safety flags. M++ calls it through this C interface: the bulk pixel
// work (format conversion, clears, blur, PNG) and the thread pool every
// dispatch and draw runs on.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Worker threads plus the caller.
long mtl_core_count(void);

/// Runs task(context, i) for i in 0..<count on the pool (the caller works
/// too). Nested calls from inside a task run serially.
typedef void (*mtl_task)(void *context, long index);
void mtl_parallel_for(long count, void *context, mtl_task task);

/// Textures are float4 texels (16 bytes); strides are in texels or words.
void mtl_fill(float *texels, long count, float r, float g, float b, float a);
void mtl_from_argb(float *dst, long dstStride, const uint32_t *src, long srcStride, long width, long height);
/// Half resolution: each dst texel averages a 2×2 block of the srcWidth × srcHeight source.
void mtl_from_argb_half(float *dst, long dstStride, const uint32_t *src, long srcStride, long srcWidth, long srcHeight);
/// blend: source-over with the texel's alpha onto what `dst` holds.
void mtl_to_argb(uint32_t *dst, long dstStride, const float *src, long srcStride, long width, long height, int blend);

/// Gaussian blur (three box passes, edges clamped), in place, any sigma at
/// the same cost.
void mtl_gaussian_blur(float *texels, long width, long height, float sigma);

/// The average of each texel and its four neighbours (edges clamped), in place.
void mtl_cross_average(float *texels, long width, long height);

/// Writes 0xAARRGGBB words as an RGBA PNG. Returns 0 on success.
int mtl_write_png(const char *path, const uint32_t *argb, long width, long height);

#ifdef __cplusplus
}
#endif

// MARK: - Shaders compiled to C++ (see MetallicShader.hpp)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float *texels; long width, height; } mtl_texture;
typedef struct { int filter, address; } mtl_sampler;          // filter: 0 nearest, 1 linear; address: 0 clamp_to_edge, 1 repeat, 2 mirrored_repeat, 3 clamp_to_zero

/// What a compiled shader reads: its [[buffer(n)]], [[texture(n)]], [[sampler(n)]].
typedef struct {
    const void *const *buffers;
    const mtl_texture *textures;
    const mtl_sampler *samplers;
} mtl_bindings;

/// A band of rows of a compute dispatch.
typedef struct { long y0, y1, width, height; } mtl_rows;

/// One row of a triangle (see MetallicSpan in Shader.mpp).
typedef struct {
    long y, x0, x1;
    float a[3], b[3], c[3];
    float inv;
    float invW[3];
    const void *va, *vb, *vc;
    float *row;
    int blending;
} mtl_span;

#ifdef __cplusplus
}
#endif
