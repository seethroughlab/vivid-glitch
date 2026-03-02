#ifndef VIVID_GLITCH_GPU_H
#define VIVID_GLITCH_GPU_H

#include <cstdint>
#include <string>

namespace glitch_gpu {

// ---------------------------------------------------------------------------
// WGSL hash functions — copy-pasted into each .wgsl file (no #include in WGSL)
// Kept here as a reference; each shader embeds these directly.
// ---------------------------------------------------------------------------
inline constexpr const char* WGSL_HASH_FUNCTIONS = R"(
fn hash11(p: f32) -> f32 {
    var x = fract(p * 0.1031);
    x = x * (x + 33.33);
    x = x * (x + x);
    return fract(x);
}

fn hash21(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.x, p.y, p.x) * 0.1031);
    p3 = p3 + dot(p3, vec3f(p3.y + 33.33, p3.z + 33.33, p3.x + 33.33));
    return fract((p3.x + p3.y) * p3.z);
}

fn hash22(p: vec2f) -> vec2f {
    var p3 = fract(vec3f(p.x, p.y, p.x) * vec3f(0.1031, 0.1030, 0.0973));
    p3 = p3 + dot(p3, vec3f(p3.y + 33.33, p3.z + 33.33, p3.x + 33.33));
    return fract(vec2f((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
}
)";

// ---------------------------------------------------------------------------
// C++ LCG random — lightweight deterministic RNG for meta-operator
// ---------------------------------------------------------------------------
struct LcgRandom {
    uint32_t state;

    explicit LcgRandom(uint32_t seed = 12345) : state(seed) {}

    uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    // Returns [0, 1)
    float next_float() {
        return static_cast<float>(next() >> 8) / 16777216.0f;
    }
};

// ---------------------------------------------------------------------------
// Helper: derive .wgsl path from __FILE__ (for package shader path override)
// ---------------------------------------------------------------------------
inline std::string wgsl_path_from_cpp(const char* cpp_file) {
    std::string path(cpp_file);
    auto dot = path.rfind('.');
    if (dot != std::string::npos) path = path.substr(0, dot);
    return path + ".wgsl";
}

} // namespace glitch_gpu

#endif // VIVID_GLITCH_GPU_H
