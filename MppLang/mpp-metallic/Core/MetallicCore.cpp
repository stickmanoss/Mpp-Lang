// Metallic's core, in C++23. See MetallicCore.h.

#include "MetallicCore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#if __cplusplus < 202302L
#error "Metallic's core is C++23"
#endif

namespace {

// MARK: - Thread pool

class Pool {
public:
    Pool() {
        const unsigned n = std::max(1u, std::thread::hardware_concurrency());
        workers_.reserve(n - 1);
        for (unsigned i = 1; i < n; ++i) workers_.emplace_back([this] { work(); });
    }
    ~Pool() {
        { std::lock_guard lock(mutex_); stopping_ = true; ++generation_; }
        wake_.notify_all();
        for (auto& w : workers_) w.join();
    }

    long size() const noexcept { return static_cast<long>(workers_.size()) + 1; }

    void run(long count, void* context, mtl_task task) {
        if (count <= 0) return;
        if (count == 1 || inside_ || workers_.empty()) {
            for (long i = 0; i < count; ++i) task(context, i);
            return;
        }
        std::unique_lock busy(dispatch_);          // one parallel job at a time
        {
            std::lock_guard lock(mutex_);
            job_ = {context, task, count};
            next_.store(0, std::memory_order_relaxed);
            active_ = static_cast<long>(workers_.size());
            ++generation_;
        }
        wake_.notify_all();
        inside_ = true;
        drain();
        inside_ = false;
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return active_ == 0; });
    }

private:
    struct Job { void* context = nullptr; mtl_task task = nullptr; long count = 0; };

    void drain() {
        for (long i; (i = next_.fetch_add(1, std::memory_order_relaxed)) < job_.count;) job_.task(job_.context, i);
    }

    void work() {
        inside_ = true;
        unsigned long seen = 0;
        while (true) {
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return generation_ != seen || stopping_; });
                if (stopping_) return;
                seen = generation_;
            }
            drain();
            std::lock_guard lock(mutex_);
            if (--active_ == 0) done_.notify_one();
        }
    }

    std::vector<std::thread> workers_;
    bool stopping_ = false;
    std::mutex mutex_, dispatch_;
    std::condition_variable wake_, done_;
    unsigned long generation_ = 0;
    long active_ = 0;
    Job job_;
    std::atomic<long> next_{0};
    static thread_local bool inside_;
};

thread_local bool Pool::inside_ = false;

Pool& pool() {
    static Pool p;
    return p;
}

/// Splits 0..<n into about two chunks per core and runs `body(lo, hi)` on the pool.
template <class F>
void parallelRanges(long n, long minChunk, F&& body) {
    if (n <= 0) return;
    const long chunks = std::clamp(n / std::max(1L, minChunk), 1L, pool().size() * 2);
    if (chunks == 1) { body(0L, n); return; }
    struct Ctx { F* f; long n, chunks; } ctx{&body, n, chunks};
    pool().run(chunks, &ctx, [](void* c, long i) {
        auto& x = *static_cast<Ctx*>(c);
        (*x.f)(x.n * i / x.chunks, x.n * (i + 1) / x.chunks);
    });
}

struct alignas(16) Texel { float r, g, b, a; };
static_assert(sizeof(Texel) == 16);

constexpr float inv255 = 1.0f / 255.0f;

inline uint32_t pack(float r, float g, float b, float a) noexcept {
    auto q = [](float v) static noexcept { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return q(a) << 24 | q(r) << 16 | q(g) << 8 | q(b);
}

// MARK: - Box blur (running sums, clamped edges)

typedef float v4 __attribute__((ext_vector_type(4)));

/// Blurs one row of n texels with a box of radius r, src -> dst.
inline void boxRow(const v4* src, v4* dst, long n, long r) {
    const float scale = 1.0f / static_cast<float>(2 * r + 1);
    v4 sum = 0;
    for (long i = -r; i <= r; ++i) sum += src[std::clamp(i, 0L, n - 1)];
    for (long i = 0; i < n; ++i) {
        dst[i] = sum * scale;
        const long in = i + r + 1, out = i - r;
        sum += src[in < n ? in : n - 1] - src[out > 0 ? out : 0];
    }
}

/// Blurs columns [x0, x1) of a width × height image with a box of radius r,
/// a block of columns at a time so each step reads one contiguous run of a row.
inline void boxColumns(const v4* src, v4* dst, long width, long height, long x0, long x1, long r) {
    constexpr long block = 32;
    const float scale = 1.0f / static_cast<float>(2 * r + 1);
    v4 sum[block];
    for (long bx = x0; bx < x1; bx += block) {
        const long n = std::min(block, x1 - bx);
        auto row = [&](long y) { return src + std::clamp(y, 0L, height - 1) * width + bx; };
        for (long k = 0; k < n; ++k) sum[k] = 0;
        for (long y = -r; y <= r; ++y) {
            const v4* p = row(y);
            for (long k = 0; k < n; ++k) sum[k] += p[k];
        }
        for (long y = 0; y < height; ++y) {
            v4* o = dst + y * width + bx;
            const v4* in = row(y + r + 1);
            const v4* out = row(y - r);
            for (long k = 0; k < n; ++k) {
                o[k] = sum[k] * scale;
                sum[k] += in[k] - out[k];
            }
        }
    }
}

/// Box radii for three passes approximating a Gaussian (Kovesi's method).
std::array<long, 3> boxRadii(float sigma) {
    const float ideal = std::sqrt(12.0f * sigma * sigma / 3.0f + 1.0f);
    long lower = static_cast<long>(std::floor(ideal));
    if (lower % 2 == 0) --lower;
    const long upper = lower + 2;
    const float m = (12.0f * sigma * sigma - 3.0f * lower * lower - 12.0f * lower - 9.0f) / (-4.0f * lower - 4.0f);
    const long small = std::lround(m);
    std::array<long, 3> out{};
    for (long i = 0; i < 3; ++i) out[i] = ((i < small ? lower : upper) - 1) / 2;
    return out;
}

// MARK: - PNG (stored deflate)

struct Crc {
    std::array<uint32_t, 256> table{};
    constexpr Crc() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    constexpr uint32_t update(uint32_t c, std::span<const uint8_t> bytes) const {
        for (uint8_t b : bytes) c = table[(c ^ b) & 255] ^ (c >> 8);
        return c;
    }
};
constexpr Crc crc;

void be32(std::vector<uint8_t>& out, uint32_t v) {
    v = std::byteswap(v);
    if constexpr (std::endian::native == std::endian::big) v = std::byteswap(v);
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + 4);
}

void chunk(std::vector<uint8_t>& file, const char (&type)[5], std::span<const uint8_t> data) {
    be32(file, static_cast<uint32_t>(data.size()));
    const auto* t = reinterpret_cast<const uint8_t*>(type);
    file.insert(file.end(), t, t + 4);
    file.insert(file.end(), data.begin(), data.end());
    be32(file, crc.update(crc.update(0xFFFFFFFFu, {t, 4}), data) ^ 0xFFFFFFFFu);
}

}  // namespace

// MARK: - C interface

extern "C" {

long mtl_core_count(void) { return pool().size(); }

void mtl_parallel_for(long count, void* context, mtl_task task) { pool().run(count, context, task); }

void mtl_fill(float* texels, long count, float r, float g, float b, float a) {
    auto* t = reinterpret_cast<Texel*>(texels);
    parallelRanges(count, 1 << 16, [=](long lo, long hi) { std::fill(t + lo, t + hi, Texel{r, g, b, a}); });
}

void mtl_from_argb(float* dst, long dstStride, const uint32_t* src, long srcStride, long width, long height) {
    auto* d = reinterpret_cast<Texel*>(dst);
    parallelRanges(height, std::max(1L, 16384 / std::max(1L, width)), [=](long y0, long y1) {
        for (long y = y0; y < y1; ++y) {
            const uint32_t* s = src + y * srcStride;
            Texel* o = d + y * dstStride;
            for (long x = 0; x < width; ++x) {
                const uint32_t p = s[x];
                o[x] = {static_cast<float>((p >> 16) & 255) * inv255, static_cast<float>((p >> 8) & 255) * inv255,
                        static_cast<float>(p & 255) * inv255, static_cast<float>(p >> 24) * inv255};
            }
        }
    });
}

void mtl_from_argb_half(float* dst, long dstStride, const uint32_t* src, long srcStride, long srcWidth, long srcHeight) {
    auto* d = reinterpret_cast<Texel*>(dst);
    const long width = (srcWidth + 1) / 2, height = (srcHeight + 1) / 2;
    parallelRanges(height, std::max(1L, 8192 / std::max(1L, width)), [=](long y0, long y1) {
        for (long y = y0; y < y1; ++y) {
            const uint32_t* r0 = src + (2 * y) * srcStride;
            const uint32_t* r1 = src + std::min(2 * y + 1, srcHeight - 1) * srcStride;
            Texel* o = d + y * dstStride;
            for (long x = 0; x < width; ++x) {
                const long xa = 2 * x, xb = std::min(2 * x + 1, srcWidth - 1);
                const uint32_t p[4] = {r0[xa], r0[xb], r1[xa], r1[xb]};
                float c[4] = {0, 0, 0, 0};
                for (uint32_t q : p) {
                    c[0] += static_cast<float>((q >> 16) & 255); c[1] += static_cast<float>((q >> 8) & 255);
                    c[2] += static_cast<float>(q & 255); c[3] += static_cast<float>(q >> 24);
                }
                const float k = inv255 * 0.25f;
                o[x] = {c[0] * k, c[1] * k, c[2] * k, c[3] * k};
            }
        }
    });
}

void mtl_to_argb(uint32_t* dst, long dstStride, const float* src, long srcStride, long width, long height, int blend) {
    const auto* s = reinterpret_cast<const Texel*>(src);
    parallelRanges(height, std::max(1L, 16384 / std::max(1L, width)), [=](long y0, long y1) {
        for (long y = y0; y < y1; ++y) {
            const Texel* i = s + y * srcStride;
            uint32_t* o = dst + y * dstStride;
            if (!blend) {
                for (long x = 0; x < width; ++x) o[x] = pack(i[x].r, i[x].g, i[x].b, i[x].a);
                continue;
            }
            for (long x = 0; x < width; ++x) {
                const float a = std::clamp(i[x].a, 0.0f, 1.0f);
                if (a <= 0.0f) continue;
                const uint32_t p = o[x];
                const float r = static_cast<float>((p >> 16) & 255) * inv255, g = static_cast<float>((p >> 8) & 255) * inv255,
                            b = static_cast<float>(p & 255) * inv255;
                o[x] = pack(r + (i[x].r - r) * a, g + (i[x].g - g) * a, b + (i[x].b - b) * a, 1.0f);
            }
        }
    });
}

void mtl_gaussian_blur(float* texels, long width, long height, float sigma) {
    if (!(sigma > 0.3f) || width <= 0 || height <= 0) return;
    auto* t = reinterpret_cast<v4*>(texels);
    std::vector<v4> scratch(static_cast<std::size_t>(width * height));
    v4* tmp = scratch.data();
    for (const long r : boxRadii(sigma)) {
        if (r <= 0) continue;
        parallelRanges(height, std::max(1L, 8192 / width), [=](long y0, long y1) {
            for (long y = y0; y < y1; ++y) boxRow(t + y * width, tmp + y * width, width, r);
        });
        parallelRanges(width, 32, [=](long x0, long x1) { boxColumns(tmp, t, width, height, x0, x1, r); });
    }
}

void mtl_cross_average(float* texels, long width, long height) {
    if (width <= 0 || height <= 0) return;
    auto* t = reinterpret_cast<Texel*>(texels);
    std::vector<Texel> copy(t, t + width * height);
    const Texel* c = copy.data();
    parallelRanges(height, std::max(1L, 8192 / width), [=](long y0, long y1) {
        for (long y = y0; y < y1; ++y) {
            const Texel* up = c + std::max(y - 1, 0L) * width;
            const Texel* mid = c + y * width;
            const Texel* down = c + std::min(y + 1, height - 1) * width;
            Texel* o = t + y * width;
            for (long x = 0; x < width; ++x) {
                const Texel& l = mid[std::max(x - 1, 0L)];
                const Texel& r = mid[std::min(x + 1, width - 1)];
                o[x] = {(mid[x].r + l.r + r.r + up[x].r + down[x].r) * 0.2f, (mid[x].g + l.g + r.g + up[x].g + down[x].g) * 0.2f,
                        (mid[x].b + l.b + r.b + up[x].b + down[x].b) * 0.2f, (mid[x].a + l.a + r.a + up[x].a + down[x].a) * 0.2f};
            }
        }
    });
}

int mtl_write_png(const char* path, const uint32_t* argb, long width, long height) {
    if (width <= 0 || height <= 0) return 1;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<std::size_t>((width * 4 + 1) * height));
    for (long y = 0; y < height; ++y) {
        raw.push_back(0);
        for (long x = 0; x < width; ++x) {
            const uint32_t p = argb[y * width + x];
            raw.insert(raw.end(), {static_cast<uint8_t>(p >> 16), static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p),
                                   static_cast<uint8_t>(p >> 24)});
        }
    }
    std::vector<uint8_t> z{0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (std::size_t off = 0; off < raw.size();) {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - off);
        z.push_back(off + len == raw.size() ? 1 : 0);
        z.insert(z.end(), {static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(~len),
                           static_cast<uint8_t>(~len >> 8)});
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(off), raw.begin() + static_cast<std::ptrdiff_t>(off + len));
        off += len;
    }
    be32(z, b << 16 | a);
    std::vector<uint8_t> file{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(width));
    be32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(file, "IHDR", ihdr);
    chunk(file, "IDAT", z);
    chunk(file, "IEND", {});
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return 1;
    const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    return (std::fclose(f) == 0 && ok) ? 0 : 1;
}

}  // extern "C"
