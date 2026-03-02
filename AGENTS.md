# AGENTS.md — vivid-glitch

## What this is

vivid-glitch is a package of creative glitch effects for [Vivid](https://vivid.works), spanning two domains:

- **Audio** (C++) — beat-synced DSP effects like stutter, reverse, tape stop, scratch, time-stretch, frequency shift
- **GPU** (C++ + WGSL shaders) — visual glitch effects like datamosh, VHS, pixel sort, static, scan distortion

There are 17 operators total (8 audio, 9 GPU). Each domain also has a meta-operator (`glitch` / `visual_glitch`) that combines multiple effects with weighted random selection.

## Directory layout

```
operators/
  audio/
    beat_repeat/        # simple repeater with decay
    freq_shift/         # Bode frequency shifter (Hilbert transform)
    glitch/             # meta-operator: weighted random effect selection
    glitch_common/      # shared DSP utilities (glitch_dsp.h)
    reverse/            # beat-triggered reverse playback ← minimal example
    scratch/            # DJ-style varispeed scratching
    stretch/            # granular time-stretch (Hann-windowed grains)
    stutter/            # beat-synced repeater with envelope shaping
    tape_stop/          # varispeed deceleration/acceleration
  gpu/
    block_displacement/ # grid-based block offset/duplication
    channel_shift/      # independent RGB channel offsets
    datamosh/           # frame-blending with optical flow (multi-pass, manual GPU)
    glitch_common/      # shared GPU utilities (glitch_gpu.h)
    jpeg_glitch/        # DCT block corruption simulation
    pixel_sort/         # luminance-based pixel displacement
    scan_distort/       # per-scanline horizontal displacement
    static_glitch/      # TV static/interference ← minimal GPU example
    vhs/                # VHS tracking/color bleed/wobble
    visual_glitch/      # meta-operator (manual GPU, multi-effect)
graphs/                 # demo JSON graphs for manual testing
tests/
  cpp/                  # placeholder (.gitkeep)
  graphs/               # placeholder (.gitkeep)
media/                  # demo audio/video clips
vivid-package.json      # package manifest
```

## Architecture

### Audio operators

Audio operators inherit from `vivid::OperatorBase` and implement three virtual methods:

```cpp
struct Reverse : vivid::OperatorBase {
    static constexpr const char* kName   = "Reverse";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    // Parameters
    vivid::Param<float> phase  {"phase",  0.0f, 0.0f, 1.0f};
    vivid::Param<float> chance {"chance", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> size   {"size",   0.25f, 0.05f, 2.0f};
    vivid::Param<float> mix    {"mix",    1.0f, 0.0f, 1.0f};

    // State — all members use trailing _
    glitch::CircularBuffer buf_;
    float prev_phase_ = 0.0f;
    enum State { Passthrough, Reversing };
    State state_ = Passthrough;

    void collect_params(std::vector<vivid::ParamBase*>& out) override { ... }
    void collect_ports(std::vector<VividPortDescriptor>& out) override { ... }
    void process(const VividProcessContext* ctx) override { ... }
};
VIVID_REGISTER(Reverse)
```

**Key patterns:**

- **Beat sync via phase**: A `phase` param (0→1 sawtooth from host) drives triggering. Use `glitch::detect_trigger(cur_phase, prev_phase_)` to detect the wrap-around, then roll against `chance` with `rng_.next_unipolar()`.
- **Circular buffer**: `glitch::CircularBuffer` (from `glitch_common/glitch_dsp.h`) provides lazy-init ring buffer with `init(sr)`, `write(sample)`, `read(double pos)`, `read_reverse(start, offset)`, and `get_read_pos(frames_back)`.
- **State machines**: Effects typically use `enum State { Passthrough, Active }` with transitions on phase trigger and completion.
- **Dry/wet mixing**: `float wet = mix.value; float dry = 1.0f - wet; out[i] = in[i] * dry + effect * wet;`
- **Click prevention**: `glitch::crossfade_coeff(pos, length, fade_samples)` applies fade-in/fade-out (typically 128 samples).
- **Randomization**: `glitch::WhiteNoise rng_` with `rng_.next_unipolar()` for [0,1) floats.

### GPU operators — WgslFilterBase (simple)

Most GPU operators subclass `vivid::WgslFilterBase`, which handles shader compilation, port setup, and rendering automatically. The C++ side is just parameters:

```cpp
struct StaticGlitch : vivid::WgslFilterBase {
    static constexpr const char* kName   = "Static Glitch";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> amount {"amount", 0.5f, 0.0f, 1.0f};
    // ...

    StaticGlitch() : WgslFilterBase("static_glitch.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
    }
};
VIVID_REGISTER(StaticGlitch)
```

The matching `.wgsl` file lives alongside the `.cpp` file. `glitch_gpu::wgsl_path_from_cpp(__FILE__)` resolves the path automatically.

### GPU operators — manual (advanced)

`visual_glitch` and `datamosh` inherit from `vivid::OperatorBase` directly and manage WGpu resources manually (pipelines, bind groups, persistent textures, multi-pass rendering). Only use this pattern when WgslFilterBase can't express what you need (e.g., multi-pass, frame history).

## Coding conventions

### Naming

| What | Style | Examples |
|------|-------|---------|
| Member variables | `snake_case` with trailing `_` | `buf_`, `prev_phase_`, `state_pos_` |
| Local variables | `snake_case` | `cur_phase`, `frames_back` |
| Constants | `k` + CamelCase | `kName`, `kMaxGrains`, `kHilbertTaps` |
| Parameters | `snake_case` (no prefix) | `phase`, `mix`, `env_amount` |
| Struct names | CamelCase | `TapeStop`, `BeatRepeat` |
| Enum values | CamelCase | `Passthrough`, `Stopping`, `Reversing` |

### Includes

```cpp
#include "operator_api/operator.h"          // always
#include "operator_api/audio_operator.h"    // audio ops (vivid_audio helper)
#include "operator_api/wgsl_filter.h"       // GPU WgslFilterBase ops
#include "operator_api/gpu_common.h"        // manual GPU ops
#include "../glitch_common/glitch_dsp.h"    // audio: CircularBuffer, crossfade, WhiteNoise
#include "../glitch_common/glitch_gpu.h"    // gpu: wgsl_path_from_cpp, LcgRandom, WGSL_HASH_FUNCTIONS
```

### Parameters

```cpp
vivid::Param<float> name {"name", default, min, max};         // float
vivid::Param<int>   name {"name", default, min, max};         // int
vivid::Param<int>   name {"name", default, {"A", "B", "C"}};  // enum (dropdown)
```

Access: `param.value` for float, `param.int_value()` for enum index.

Optional display hints before pushing to `out`:
```cpp
display_hint(param, VIVID_DISPLAY_XY_PAD);
```

### Ports

Audio operators declare ports explicitly:
```cpp
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
    out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
}
```

WgslFilterBase handles ports automatically (no override needed).

### Process loop (audio)

```cpp
void process(const VividProcessContext* ctx) override {
    auto* audio = vivid_audio(ctx);
    if (!audio) return;

    float* in  = audio->input_buffers[0];
    float* out = audio->output_buffers[0];
    uint32_t frames = audio->buffer_size;
    uint32_t sr     = audio->sample_rate;

    for (uint32_t i = 0; i < frames; i++) {
        // ...
    }
}
```

## How to add a new operator

### New audio operator

1. Create `operators/audio/my_effect/my_effect.cpp`
2. Include `operator_api/operator.h`, `operator_api/audio_operator.h`, and optionally `../glitch_common/glitch_dsp.h`
3. Define a struct inheriting `vivid::OperatorBase` with `kName`, `kDomain = VIVID_DOMAIN_AUDIO`, `kTimeDependent`
4. Declare params, implement `collect_params`, `collect_ports`, `process`
5. End file with `VIVID_REGISTER(MyEffect)`
6. Add `"audio/my_effect"` to the `"operators"` array in `vivid-package.json`

Reference: `operators/audio/reverse/reverse.cpp` (minimal), `operators/audio/stutter/stutter.cpp` (envelopes + state)

### New GPU operator (WgslFilterBase)

1. Create `operators/gpu/my_effect/my_effect.cpp` and `operators/gpu/my_effect/my_effect.wgsl`
2. In the `.cpp`: inherit `vivid::WgslFilterBase`, set `kDomain = VIVID_DOMAIN_GPU`, call `WgslFilterBase("my_effect.wgsl")` and `set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__))` in constructor
3. Declare params and implement `collect_params`
4. Write the WGSL fragment shader in the `.wgsl` file (parameters arrive as uniforms)
5. End `.cpp` with `VIVID_REGISTER(MyEffect)`
6. Add `"gpu/my_effect"` to the **`"gpu_operators"`** array in `vivid-package.json`

Reference: `operators/gpu/static_glitch/static_glitch.cpp` (minimal)

## Manifest (vivid-package.json)

Audio operators go in `"operators"`, GPU operators go in `"gpu_operators"`. This distinction is **critical** — the package compiler uses the key to pass correct compilation flags. Getting it wrong causes silent build failures.

```json
{
  "operators": ["audio/stutter", "audio/reverse", ...],
  "gpu_operators": ["gpu/static_glitch", "gpu/datamosh", ...]
}
```

## Known issues

- **wgpu linker flag**: Vivid's `package_compiler.cpp` doesn't link `-lwgpu_native` for GPU operators out-of-box. A local fix has been applied. Waiting on upstream fix.
- **gpu_operators key**: GPU operators must be listed under `"gpu_operators"` in the manifest, not `"operators"`. Non-obvious; incorrect key → silent compilation failure.
- **Addon system refactor**: The package/addon system is being refactored upstream. Manifest format and compilation procedures may change.

## Testing

No automated tests exist yet (scaffolding only in `tests/`). Test operators manually by:

1. Creating a graph JSON in `graphs/` that wires the operator to audio/video sources
2. Loading the graph in Vivid and verifying behavior
3. Existing demo graphs (e.g., `glitch_demo.json`, `av_glitch_demo.json`) show wiring patterns

## Build

There is no CMakeLists.txt — compilation is handled by Vivid's package system (`vivid packages install`). Operators are discovered and compiled based on manifest entries.
