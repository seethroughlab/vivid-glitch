#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// =============================================================================
// Datamosh WGSL Shaders (inline — hand-coded multi-pass operator)
// =============================================================================

static const char* kMotionFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    frame: u32,
    amount: f32,
    quantize_motion: f32,
    _pad0: f32,
    _pad1: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var currentTex: texture_2d<f32>;
@group(0) @binding(3) var prevTex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn luma(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let px = 1.0 / uniforms.resolution;
    let uv = input.uv;

    // Current and previous frame luminance
    let cur = luma(textureSample(currentTex, texSampler, uv).rgb);
    let prev = luma(textureSample(prevTex, texSampler, uv).rgb);

    // Spatial gradients (Sobel-like) on current frame
    let dx = luma(textureSample(currentTex, texSampler, uv + vec2f(px.x, 0.0)).rgb)
           - luma(textureSample(currentTex, texSampler, uv - vec2f(px.x, 0.0)).rgb);
    let dy = luma(textureSample(currentTex, texSampler, uv + vec2f(0.0, px.y)).rgb)
           - luma(textureSample(currentTex, texSampler, uv - vec2f(0.0, px.y)).rgb);

    // Temporal gradient
    let dt = cur - prev;

    // Lucas-Kanade style: v = -dt * gradient / (gradient^2 + epsilon)
    let grad_sq = dx * dx + dy * dy + 0.0001;
    var vx = -dt * dx / grad_sq;
    var vy = -dt * dy / grad_sq;

    // Scale by amount
    vx = vx * uniforms.amount;
    vy = vy * uniforms.amount;

    // Optional motion quantization
    if (uniforms.quantize_motion > 0.0) {
        let q = max(uniforms.quantize_motion * 16.0, 1.0);
        vx = round(vx * q) / q;
        vy = round(vy * q) / q;
    }

    // Clamp motion magnitude
    let mag = sqrt(vx * vx + vy * vy);
    let max_mag = 0.1;
    if (mag > max_mag) {
        vx = vx * max_mag / mag;
        vy = vy * max_mag / mag;
    }

    // Output: RG = motion vector, B = magnitude, A = 1
    return vec4f(vx * 0.5 + 0.5, vy * 0.5 + 0.5, min(mag * 10.0, 1.0), 1.0);
}
)";

static const char* kCompositeFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    frame: u32,
    amount: f32,
    freeze: f32,
    displacement: f32,
    blend: f32,
    reset_phase: f32,
    color_bleed: f32,
    mix_val: f32,
    _pad: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var currentTex: texture_2d<f32>;
@group(0) @binding(3) var prevTex: texture_2d<f32>;
@group(0) @binding(4) var motionTex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let current = textureSample(currentTex, texSampler, uv);

    // Read motion vectors
    let motion_raw = textureSample(motionTex, texSampler, uv);
    let mv = (motion_raw.rg - 0.5) * 2.0; // Decode from [0,1] to [-1,1]
    let motion_mag = motion_raw.b;

    // Displace previous frame by motion vectors
    let disp_uv = uv + mv * uniforms.displacement;
    let prev_displaced = textureSample(prevTex, texSampler, disp_uv);

    // Per-channel offset for color bleed
    var moshed: vec3f;
    if (uniforms.color_bleed > 0.0) {
        let bleed_off = mv * uniforms.color_bleed * 0.5;
        let r = textureSample(prevTex, texSampler, disp_uv + bleed_off * 0.7).r;
        let g = prev_displaced.g;
        let b = textureSample(prevTex, texSampler, disp_uv - bleed_off * 0.3).b;
        moshed = vec3f(r, g, b);
    } else {
        moshed = prev_displaced.rgb;
    }

    // Blend: freeze=1 means fully use previous, freeze=0 blends with current
    let blend_weight = uniforms.blend * (1.0 + uniforms.freeze);
    let blend_factor = clamp(blend_weight * (0.5 + motion_mag), 0.0, 1.0);

    // Reset detection: when reset_phase > 0.5, show more current frame (I-frame)
    let reset = smoothstep(0.4, 0.6, uniforms.reset_phase);
    let effective_blend = blend_factor * (1.0 - reset);

    let result = mix(current.rgb, moshed, effective_blend);
    let final_color = vec4f(result, current.a);

    return mix(current, final_color, uniforms.mix_val);
}
)";

// =============================================================================
// Uniform structs matching WGSL
// =============================================================================

struct MotionUniforms {
    float resolution[2];
    float time;
    uint32_t frame;
    float amount;
    float quantize_motion;
    float _pad0, _pad1;
};

struct CompositeUniforms {
    float resolution[2];
    float time;
    uint32_t frame;
    float amount;
    float freeze;
    float displacement;
    float blend;
    float reset_phase;
    float color_bleed;
    float mix_val;
    float _pad;
};

// =============================================================================
// Datamosh Operator
// =============================================================================

struct Datamosh : vivid::OperatorBase {
    static constexpr const char* kName   = "Datamosh";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> amount          {"amount",          0.5f, 0.0f, 1.0f};
    vivid::Param<float> freeze          {"freeze",          0.0f, 0.0f, 1.0f};
    vivid::Param<float> displacement    {"displacement",    0.3f, 0.0f, 1.0f};
    vivid::Param<float> blend           {"blend",           0.7f, 0.0f, 1.0f};
    vivid::Param<float> reset_phase     {"reset_phase",     0.0f, 0.0f, 1.0f};
    vivid::Param<float> quantize_motion {"quantize_motion", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> color_bleed     {"color_bleed",     0.2f, 0.0f, 1.0f};
    vivid::Param<float> mix             {"mix",             1.0f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
        out.push_back(&freeze);
        out.push_back(&displacement);
        out.push_back(&blend);
        out.push_back(&reset_phase);
        out.push_back(&quantize_motion);
        out.push_back(&color_bleed);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!motion_pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[datamosh] lazy_init FAILED\n");
                return;
            }
        }

        // Get input texture
        WGPUTextureView input_tex = nullptr;
        if (gpu->input_texture_views && gpu->input_texture_count >= 1)
            input_tex = gpu->input_texture_views[0];
        if (!input_tex && !fallback_view_) create_fallback(gpu);
        if (!input_tex) input_tex = fallback_view_;

        // Recreate persistent textures on resolution change
        if (gpu->output_width != cached_width_ || gpu->output_height != cached_height_) {
            recreate_textures(gpu);
            cached_width_  = gpu->output_width;
            cached_height_ = gpu->output_height;
        }

        // Rebuild bind groups if input changed
        if (input_tex != cached_input_tex_ || bind_groups_dirty_) {
            rebuild_bind_groups(gpu, input_tex);
            cached_input_tex_ = input_tex;
            bind_groups_dirty_ = false;
        }

        // --- Pass 1: Motion estimation ---
        {
            MotionUniforms mu{};
            mu.resolution[0] = static_cast<float>(gpu->output_width);
            mu.resolution[1] = static_cast<float>(gpu->output_height);
            mu.time = static_cast<float>(ctx->time);
            mu.frame = static_cast<uint32_t>(ctx->frame);
            mu.amount = amount.value;
            mu.quantize_motion = quantize_motion.value;
            wgpuQueueWriteBuffer(gpu->queue, motion_uniform_buf_, 0, &mu, sizeof(mu));

            vivid::gpu::run_pass(gpu->command_encoder, motion_pipeline_, motion_bg_,
                                 motion_view_, "Datamosh Motion");
        }

        // --- Pass 2: Composite ---
        {
            CompositeUniforms cu{};
            cu.resolution[0] = static_cast<float>(gpu->output_width);
            cu.resolution[1] = static_cast<float>(gpu->output_height);
            cu.time = static_cast<float>(ctx->time);
            cu.frame = static_cast<uint32_t>(ctx->frame);
            cu.amount = amount.value;
            cu.freeze = freeze.value;
            cu.displacement = displacement.value;
            cu.blend = blend.value;
            cu.reset_phase = reset_phase.value;
            cu.color_bleed = color_bleed.value;
            cu.mix_val = mix.value;
            wgpuQueueWriteBuffer(gpu->queue, composite_uniform_buf_, 0, &cu, sizeof(cu));

            vivid::gpu::run_pass(gpu->command_encoder, composite_pipeline_, composite_bg_,
                                 gpu->output_texture_view, "Datamosh Composite");
        }

        // --- Copy output to prev_frame for next frame ---
        {
            WGPUTexelCopyTextureInfo src{};
            src.texture = gpu->output_texture;
            WGPUTexelCopyTextureInfo dst{};
            dst.texture = prev_tex_;
            WGPUExtent3D size = { gpu->output_width, gpu->output_height, 1 };
            wgpuCommandEncoderCopyTextureToTexture(gpu->command_encoder, &src, &dst, &size);
        }
    }

    ~Datamosh() override {
        vivid::gpu::release(motion_pipeline_);
        vivid::gpu::release(composite_pipeline_);
        vivid::gpu::release(motion_layout_);
        vivid::gpu::release(composite_layout_);
        vivid::gpu::release(motion_pipe_layout_);
        vivid::gpu::release(composite_pipe_layout_);
        vivid::gpu::release(motion_shader_);
        vivid::gpu::release(composite_shader_);
        vivid::gpu::release(motion_uniform_buf_);
        vivid::gpu::release(composite_uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(motion_bg_);
        vivid::gpu::release(composite_bg_);
        vivid::gpu::release(prev_tex_);
        vivid::gpu::release(prev_view_);
        vivid::gpu::release(motion_tex_);
        vivid::gpu::release(motion_view_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    // Pipelines
    WGPURenderPipeline motion_pipeline_    = nullptr;
    WGPURenderPipeline composite_pipeline_ = nullptr;

    // Layouts
    WGPUBindGroupLayout motion_layout_     = nullptr; // uniform + sampler + 2 tex
    WGPUBindGroupLayout composite_layout_  = nullptr; // uniform + sampler + 3 tex
    WGPUPipelineLayout  motion_pipe_layout_    = nullptr;
    WGPUPipelineLayout  composite_pipe_layout_ = nullptr;

    // Shaders
    WGPUShaderModule motion_shader_    = nullptr;
    WGPUShaderModule composite_shader_ = nullptr;

    // Shared resources
    WGPUBuffer  motion_uniform_buf_    = nullptr;
    WGPUBuffer  composite_uniform_buf_ = nullptr;
    WGPUSampler sampler_               = nullptr;

    // Bind groups
    WGPUBindGroup motion_bg_    = nullptr;
    WGPUBindGroup composite_bg_ = nullptr;

    // Persistent textures
    WGPUTexture     prev_tex_   = nullptr;
    WGPUTextureView prev_view_  = nullptr;
    WGPUTexture     motion_tex_ = nullptr;
    WGPUTextureView motion_view_ = nullptr;

    // Fallback
    WGPUTexture     fallback_tex_  = nullptr;
    WGPUTextureView fallback_view_ = nullptr;

    // Cache
    WGPUTextureView cached_input_tex_ = nullptr;
    uint32_t cached_width_  = 0;
    uint32_t cached_height_ = 0;
    bool bind_groups_dirty_ = true;

    // -------------------------------------------------------------------------
    void create_fallback(VividGpuState* gpu) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Datamosh Fallback");
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
    WGPUTexture create_persistent_texture(VividGpuState* gpu, const char* label,
                                           WGPUTextureFormat format) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv(label);
        td.size          = { gpu->output_width, gpu->output_height, 1 };
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = format;
        td.usage         = WGPUTextureUsage_TextureBinding |
                           WGPUTextureUsage_RenderAttachment |
                           WGPUTextureUsage_CopyDst;
        return wgpuDeviceCreateTexture(gpu->device, &td);
    }

    WGPUTextureView create_view(WGPUTexture tex, WGPUTextureFormat format) {
        WGPUTextureViewDescriptor vd{};
        vd.format          = format;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        vd.aspect          = WGPUTextureAspect_All;
        return wgpuTextureCreateView(tex, &vd);
    }

    // -------------------------------------------------------------------------
    void recreate_textures(VividGpuState* gpu) {
        vivid::gpu::release(prev_tex_);
        vivid::gpu::release(prev_view_);
        vivid::gpu::release(motion_tex_);
        vivid::gpu::release(motion_view_);

        prev_tex_    = create_persistent_texture(gpu, "Datamosh Prev", gpu->output_format);
        prev_view_   = create_view(prev_tex_, gpu->output_format);
        motion_tex_  = create_persistent_texture(gpu, "Datamosh Motion", gpu->output_format);
        motion_view_ = create_view(motion_tex_, gpu->output_format);

        bind_groups_dirty_ = true;
    }

    // -------------------------------------------------------------------------
    void rebuild_bind_groups(VividGpuState* gpu, WGPUTextureView input_tex) {
        vivid::gpu::release(motion_bg_);
        vivid::gpu::release(composite_bg_);

        // Motion: uniform + sampler + currentTex + prevTex
        {
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = motion_uniform_buf_;
            entries[0].size    = sizeof(MotionUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = input_tex;
            entries[3].binding = 3;
            entries[3].textureView = prev_view_;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("Datamosh Motion BG");
            desc.layout     = motion_layout_;
            desc.entryCount = 4;
            desc.entries    = entries;
            motion_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Composite: uniform + sampler + currentTex + prevTex + motionTex
        {
            WGPUBindGroupEntry entries[5]{};
            entries[0].binding = 0;
            entries[0].buffer  = composite_uniform_buf_;
            entries[0].size    = sizeof(CompositeUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = input_tex;
            entries[3].binding = 3;
            entries[3].textureView = prev_view_;
            entries[4].binding = 4;
            entries[4].textureView = motion_view_;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("Datamosh Composite BG");
            desc.layout     = composite_layout_;
            desc.entryCount = 5;
            desc.entries    = entries;
            composite_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }
    }

    // -------------------------------------------------------------------------
    WGPUBindGroupLayout create_bind_layout(VividGpuState* gpu, uint32_t tex_count,
                                            uint32_t uniform_size, const char* label) {
        std::vector<WGPUBindGroupLayoutEntry> entries(2 + tex_count, WGPUBindGroupLayoutEntry{});

        entries[0].binding    = 0;
        entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = uniform_size;

        entries[1].binding    = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        for (uint32_t i = 0; i < tex_count; ++i) {
            entries[2 + i].binding    = 2 + i;
            entries[2 + i].visibility = WGPUShaderStage_Fragment;
            entries[2 + i].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2 + i].texture.multisampled  = false;
        }

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label      = vivid_sv(label);
        bgl_desc.entryCount = static_cast<uint32_t>(entries.size());
        bgl_desc.entries    = entries.data();
        return wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
    }

    // -------------------------------------------------------------------------
    bool lazy_init(VividGpuState* gpu) {
        // Compile shaders
        motion_shader_ = vivid::gpu::create_shader(gpu->device, kMotionFragment, "Datamosh Motion Shader");
        composite_shader_ = vivid::gpu::create_shader(gpu->device, kCompositeFragment, "Datamosh Composite Shader");
        if (!motion_shader_ || !composite_shader_) return false;

        // Shared resources
        motion_uniform_buf_    = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(MotionUniforms), "Datamosh Motion Uniforms");
        composite_uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(CompositeUniforms), "Datamosh Composite Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Datamosh Sampler");

        // Bind group layouts
        motion_layout_    = create_bind_layout(gpu, 2, sizeof(MotionUniforms), "Datamosh Motion BGL");
        composite_layout_ = create_bind_layout(gpu, 3, sizeof(CompositeUniforms), "Datamosh Composite BGL");

        // Pipeline layouts
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("Datamosh Motion PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts     = &motion_layout_;
            motion_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("Datamosh Composite PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts     = &composite_layout_;
            composite_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }

        // Render pipelines
        motion_pipeline_ = vivid::gpu::create_pipeline(gpu->device, motion_shader_,
            motion_pipe_layout_, gpu->output_format, "Datamosh Motion Pipeline");
        composite_pipeline_ = vivid::gpu::create_pipeline(gpu->device, composite_shader_,
            composite_pipe_layout_, gpu->output_format, "Datamosh Composite Pipeline");

        if (!motion_pipeline_ || !composite_pipeline_) return false;

        // Create persistent textures
        recreate_textures(gpu);

        return true;
    }
};

VIVID_REGISTER(Datamosh)
