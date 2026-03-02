#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "../glitch_common/glitch_gpu.h"
#include "operator_api/audio_dsp.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// =============================================================================
// Visual Glitch — meta-operator with inline simplified effects
// =============================================================================

static const char* kVisualGlitchFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    frame: u32,
    // Effect control (set by C++)
    active_effect: f32,
    progress: f32,
    intensity: f32,
    mix_val: f32,
    // Random seeds (set per trigger)
    seed1: f32,
    seed2: f32,
    seed3: f32,
    seed4: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
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

const PI: f32 = 3.14159265358979323846;

// Effect 1: Channel Shift
fn fx_channel_shift(uv: vec2f, intensity: f32, seed: f32) -> vec4f {
    let off = (seed - 0.5) * intensity * 0.08;
    let r = textureSample(inputTex, texSampler, uv + vec2f(off, 0.0)).r;
    let g = textureSample(inputTex, texSampler, uv).g;
    let b = textureSample(inputTex, texSampler, uv - vec2f(off, off * 0.5)).b;
    let a = textureSample(inputTex, texSampler, uv).a;
    return vec4f(r, g, b, a);
}

// Effect 2: Block Displacement
fn fx_block_disp(uv: vec2f, intensity: f32, seed: f32, res: vec2f) -> vec4f {
    let grid = 8.0 + seed * 16.0;
    let block = floor(uv * grid);
    let block_rand = hash21(block + vec2f(seed * 100.0, 0.0));
    var sample_uv = uv;
    if (block_rand > 0.6) {
        let disp = (hash22(block + vec2f(seed * 50.0, 77.0)) - 0.5) * intensity * 0.15;
        sample_uv = uv + disp;
    }
    return textureSample(inputTex, texSampler, sample_uv);
}

// Effect 3: Scan Distort
fn fx_scan_distort(uv: vec2f, intensity: f32, seed: f32, time: f32, res: vec2f) -> vec4f {
    let scanline = floor(uv.y * res.y);
    let noise = hash21(vec2f(scanline, seed * 100.0 + floor(time * 10.0)));
    let band = smoothstep(0.0, 0.1, fract(uv.y * 5.0 + seed)) *
               (1.0 - smoothstep(0.15, 0.25, fract(uv.y * 5.0 + seed)));
    let offset_x = (noise - 0.5) * intensity * 0.1 * band;
    return textureSample(inputTex, texSampler, vec2f(uv.x + offset_x, uv.y));
}

// Effect 4: Static/Snow
fn fx_static(uv: vec2f, intensity: f32, seed: f32, time: f32, res: vec2f) -> vec4f {
    let original = textureSample(inputTex, texSampler, uv);
    let noise = hash21(uv * res + vec2f(floor(time * 30.0), seed * 100.0));
    return mix(original, vec4f(vec3f(noise), original.a), intensity * 0.7);
}

// Effect 5: Pixelate
fn fx_pixelate(uv: vec2f, intensity: f32, seed: f32) -> vec4f {
    let block_size = mix(2.0, 32.0, intensity * (0.5 + seed * 0.5));
    let pixelated_uv = floor(uv * block_size) / block_size + 0.5 / block_size;
    return textureSample(inputTex, texSampler, pixelated_uv);
}

// Effect 6: Invert
fn fx_invert(uv: vec2f, intensity: f32) -> vec4f {
    let original = textureSample(inputTex, texSampler, uv);
    let inverted = vec3f(1.0) - original.rgb;
    return vec4f(mix(original.rgb, inverted, intensity), original.a);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);
    let effect = i32(u.active_effect + 0.5);
    let progress = u.progress;
    let intensity = u.intensity;
    let wet = u.mix_val;

    // No active effect
    if (effect == 0) {
        return original;
    }

    // Envelope: fade in first 10%, fade out last 20%
    let env = smoothstep(0.0, 0.1, progress) * (1.0 - smoothstep(0.8, 1.0, progress));
    let eff_intensity = intensity * env;

    var result: vec4f;

    if (effect == 1) {
        result = fx_channel_shift(input.uv, eff_intensity, u.seed1);
    } else if (effect == 2) {
        result = fx_block_disp(input.uv, eff_intensity, u.seed1, u.resolution);
    } else if (effect == 3) {
        result = fx_scan_distort(input.uv, eff_intensity, u.seed1, u.time, u.resolution);
    } else if (effect == 4) {
        result = fx_static(input.uv, eff_intensity, u.seed1, u.time, u.resolution);
    } else if (effect == 5) {
        result = fx_pixelate(input.uv, eff_intensity, u.seed1);
    } else if (effect == 6) {
        result = fx_invert(input.uv, eff_intensity);
    } else {
        result = original;
    }

    return mix(original, result, wet);
}
)";

// =============================================================================
// Uniform struct matching WGSL (48 bytes)
// =============================================================================

struct VisualGlitchUniforms {
    float resolution[2];
    float time;
    uint32_t frame;
    float active_effect;
    float progress;
    float intensity;
    float mix_val;
    float seed1;
    float seed2;
    float seed3;
    float seed4;
};

// =============================================================================
// Visual Glitch Operator
// =============================================================================

struct VisualGlitch : vivid::OperatorBase {
    static constexpr const char* kName   = "Visual Glitch";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase          {"phase",           0.0f, 0.0f, 1.0f};
    vivid::Param<float> shift_chance   {"shift_chance",    0.2f, 0.0f, 1.0f};
    vivid::Param<float> block_chance   {"block_chance",    0.2f, 0.0f, 1.0f};
    vivid::Param<float> scan_chance    {"scan_chance",     0.15f, 0.0f, 1.0f};
    vivid::Param<float> static_chance  {"static_chance",   0.15f, 0.0f, 1.0f};
    vivid::Param<float> pixelate_chance{"pixelate_chance", 0.1f, 0.0f, 1.0f};
    vivid::Param<float> invert_chance  {"invert_chance",   0.1f, 0.0f, 1.0f};
    vivid::Param<float> intensity      {"intensity",       0.7f, 0.0f, 1.0f};
    vivid::Param<float> duration       {"duration",        0.2f, 0.05f, 2.0f};
    vivid::Param<float> mix            {"mix",             1.0f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&shift_chance);
        out.push_back(&block_chance);
        out.push_back(&scan_chance);
        out.push_back(&static_chance);
        out.push_back(&pixelate_chance);
        out.push_back(&invert_chance);
        out.push_back(&intensity);
        out.push_back(&duration);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) return;
        }

        // --- Phase trigger detection ---
        float cur_phase = phase.value;
        if (audio_dsp::detect_trigger(cur_phase, prev_phase_) && active_effect_ == 0) {
            select_and_start_effect(ctx->time);
        }
        prev_phase_ = cur_phase;

        // --- Update effect progress ---
        float active_effect_f = 0.0f;
        float progress = 0.0f;
        if (active_effect_ != 0) {
            float elapsed = static_cast<float>(ctx->time - effect_start_time_);
            progress = elapsed / duration.value;
            if (progress >= 1.0f) {
                active_effect_ = 0;
                progress = 0.0f;
            } else {
                active_effect_f = static_cast<float>(active_effect_);
            }
        }

        // --- Get input texture ---
        WGPUTextureView input_tex = nullptr;
        if (gpu->input_texture_views && gpu->input_texture_count >= 1)
            input_tex = gpu->input_texture_views[0];
        if (!input_tex && !fallback_view_) create_fallback(gpu);
        if (!input_tex) input_tex = fallback_view_;

        if (input_tex != cached_input_tex_) {
            rebuild_bind_group(gpu, input_tex);
            cached_input_tex_ = input_tex;
        }

        // --- Write uniforms ---
        VisualGlitchUniforms u{};
        u.resolution[0] = static_cast<float>(gpu->output_width);
        u.resolution[1] = static_cast<float>(gpu->output_height);
        u.time          = static_cast<float>(ctx->time);
        u.frame         = static_cast<uint32_t>(ctx->frame);
        u.active_effect = active_effect_f;
        u.progress      = progress;
        u.intensity     = intensity.value;
        u.mix_val       = mix.value;
        u.seed1         = seed1_;
        u.seed2         = seed2_;
        u.seed3         = seed3_;
        u.seed4         = seed4_;
        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(gpu->command_encoder, pipeline_, bind_group_,
                             gpu->output_texture_view, "Visual Glitch");
    }

    ~VisualGlitch() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    // GPU resources
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUTexture         fallback_tex_  = nullptr;
    WGPUTextureView     fallback_view_ = nullptr;
    WGPUTextureView     cached_input_tex_ = nullptr;

    // Effect state (managed in C++)
    float prev_phase_       = 0.0f;
    int   active_effect_    = 0; // 0=none, 1-6 = effect
    double effect_start_time_ = 0.0;
    float seed1_ = 0.0f, seed2_ = 0.0f, seed3_ = 0.0f, seed4_ = 0.0f;
    glitch_gpu::LcgRandom rng_{42};

    // -------------------------------------------------------------------------
    void select_and_start_effect(double time) {
        float chances[] = {
            shift_chance.value,
            block_chance.value,
            scan_chance.value,
            static_chance.value,
            pixelate_chance.value,
            invert_chance.value
        };

        float total = 0.0f;
        for (int i = 0; i < 6; i++) total += chances[i];
        if (total <= 0.0f) return;

        float roll = rng_.next_float() * total;
        float cumulative = 0.0f;
        int selected = 6; // invert as fallback
        for (int i = 0; i < 6; i++) {
            cumulative += chances[i];
            if (roll < cumulative) {
                selected = i + 1;
                break;
            }
        }

        active_effect_ = selected;
        effect_start_time_ = time;
        seed1_ = rng_.next_float();
        seed2_ = rng_.next_float();
        seed3_ = rng_.next_float();
        seed4_ = rng_.next_float();
    }

    // -------------------------------------------------------------------------
    void create_fallback(VividGpuState* gpu) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("VisualGlitch Fallback");
        td.size          = { 1, 1, 1 };
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = gpu->output_format;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_    = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format          = gpu->output_format;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        vd.aspect          = WGPUTextureAspect_All;
        fallback_view_     = wgpuTextureCreateView(fallback_tex_, &vd);

        const uint8_t zero[8] = {};
        WGPUTexelCopyTextureInfo dest{};
        dest.texture = fallback_tex_;
        dest.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = { 1, 1, 1 };
        wgpuQueueWriteTexture(gpu->queue, &dest, zero, sizeof(zero), &layout, &extent);
    }

    // -------------------------------------------------------------------------
    void rebuild_bind_group(VividGpuState* gpu, WGPUTextureView input_tex) {
        vivid::gpu::release(bind_group_);

        WGPUBindGroupEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].buffer  = uniform_buf_;
        entries[0].size    = sizeof(VisualGlitchUniforms);
        entries[1].binding = 1;
        entries[1].sampler = sampler_;
        entries[2].binding = 2;
        entries[2].textureView = input_tex;

        WGPUBindGroupDescriptor desc{};
        desc.label      = vivid_sv("VisualGlitch BG");
        desc.layout     = bind_layout_;
        desc.entryCount = 3;
        desc.entries    = entries;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    // -------------------------------------------------------------------------
    bool lazy_init(VividGpuState* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kVisualGlitchFragment, "VisualGlitch Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(VisualGlitchUniforms), "VisualGlitch Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "VisualGlitch Sampler");

        // Bind group layout: uniform + sampler + 1 texture
        {
            WGPUBindGroupLayoutEntry entries[3]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(VisualGlitchUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled  = false;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label      = vivid_sv("VisualGlitch BGL");
            bgl_desc.entryCount = 3;
            bgl_desc.entries    = entries;
            bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Pipeline layout
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("VisualGlitch PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts     = &bind_layout_;
            pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                  gpu->output_format, "VisualGlitch Pipeline");
        return pipeline_ != nullptr;
    }
};

VIVID_REGISTER(VisualGlitch)
