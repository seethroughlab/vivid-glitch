# vivid-glitch

Creative audio and visual glitch effects for [Vivid](https://github.com/vivid-project/vivid).

## Audio Operators

| Operator | Description |
|----------|-------------|
| **Stutter** | Beat-synced slice repeater with configurable volume envelopes (decay, build, flat, triangle) |
| **TapeStop** | Tape deck slowdown/speedup simulation with cubic deceleration |
| **BeatRepeat** | Beat-synced slice repeater with per-repeat volume decay |
| **Reverse** | Beat-synced reverse playback with crossfade |
| **Scratch** | DJ-style varispeed scratch with direction control (back-forth, forward, backward, random) |
| **Stretch** | Granular time-stretch (pitch preserved) with Hann-windowed grains |
| **FreqShift** | Bode frequency shifter via 31-tap FIR Hilbert transform with LFO modulation |
| **Glitch** | Meta-effect — random audio effect selector combining all above effects with per-effect probability |

## Visual Operators

| Operator | Description |
|----------|-------------|
| **JPEG Glitch** | Simulated DCT block corruption with quantization banding and per-block channel offset |
| **Pixel Sort** | Luminance-based displacement approximating pixel sorting along rows/columns/diagonals |
| **Block Displacement** | Grid-based random block offset and duplication with time-varying patterns |
| **Scan Distort** | Per-scanline horizontal displacement simulating analog signal corruption (noise, sine, square, sawtooth) |
| **Channel Shift** | Independent R/G/B channel offsets with clamp/repeat/mirror wrapping and sine animation |
| **VHS** | Analog tape playback artifacts — tracking errors, color bleed, head switching, wobble, tape noise |
| **Static Glitch** | TV static/snow with flickering, interference patterns, scanlines, and vertical hold loss |
| **Datamosh** | Frame blending and motion displacement simulating I-frame deletion with persistent frame buffer |
| **Visual Glitch** | Meta-effect — random visual effect selector triggered by beat phase with per-effect probability |

## Install

```bash
vivid packages install https://github.com/vivid-project/vivid-glitch
```

Or install from the in-app package browser: **Browse Packages > vivid-glitch > Install**.

## Usage

Audio glitch operators have a `phase` input for beat sync and a `mix` control for dry/wet blending.

```json
{
  "nodes": {
    "clock": { "type": "Clock", "params": { "bpm": 140.0 } },
    "synth": { "type": "Oscillator", "params": { "freq": 440.0 } },
    "stutter": { "type": "Stutter", "params": { "chance": 0.5, "size": 0.1, "count": 8, "mix": 1.0 } },
    "out": { "type": "audio_out" }
  },
  "connections": [
    { "from": "clock/beat_phase", "to": "stutter/phase" },
    { "from": "synth/output", "to": "stutter/input" },
    { "from": "stutter/output", "to": "out/input" }
  ]
}
```

Visual glitch operators take a GPU texture input and output. Connect them inline in your video chain:

```json
{
  "nodes": {
    "cam": { "type": "Camera" },
    "vhs": { "type": "VHS", "params": { "tracking": 0.5, "color_bleed": 0.6, "noise": 0.3 } },
    "out": { "type": "gpu_out" }
  },
  "connections": [
    { "from": "cam/texture", "to": "vhs/input" },
    { "from": "vhs/texture", "to": "out/input" }
  ]
}
```

The **Visual Glitch** meta-operator randomly triggers visual effects on beat, mirroring the audio **Glitch** operator:

```json
{
  "nodes": {
    "clock": { "type": "Clock", "params": { "bpm": 128.0 } },
    "cam": { "type": "Camera" },
    "vglitch": { "type": "Visual Glitch", "params": { "intensity": 0.8, "duration": 0.15 } },
    "out": { "type": "gpu_out" }
  },
  "connections": [
    { "from": "clock/beat_phase", "to": "vglitch/phase" },
    { "from": "cam/texture", "to": "vglitch/input" },
    { "from": "vglitch/texture", "to": "out/input" }
  ]
}
```

## Status

All 17 operators (8 audio + 9 GPU visual) compile and install successfully.

**Verified rendering:**
- Static Glitch, Channel Shift, VHS, JPEG Glitch

**Compiled but not yet visually verified:**
- Pixel Sort, Block Displacement, Scan Distort, Datamosh, Visual Glitch

## Known Issues

- **Package compiler fix required**: Vivid's `package_compiler.cpp` doesn't link `-lwgpu_native` for GPU package operators, causing link failures. We patched it locally (added `-L`/`-lwgpu_native` flags). This fix needs to land upstream in Vivid before GPU package operators work out of the box.
- **`gpu_operators` manifest key**: GPU operators must be listed under `"gpu_operators"` (not `"operators"`) in `vivid-package.json` so the package compiler passes the correct flags. This is a non-obvious requirement.
- **Addon system refactor pending**: The package/addon system is being refactored, which may change manifest format and compilation.

## License

MIT
