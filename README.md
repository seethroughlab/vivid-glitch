# vivid-glitch

Creative audio glitch effects for [Vivid](https://github.com/vivid-project/vivid).

## Operators

| Operator | Description |
|----------|-------------|
| **Stutter** | Beat-synced slice repeater with configurable volume envelopes (decay, build, flat, triangle) |
| **TapeStop** | Tape deck slowdown/speedup simulation with cubic deceleration |
| **BeatRepeat** | Beat-synced slice repeater with per-repeat volume decay |
| **Reverse** | Beat-synced reverse playback with crossfade |
| **Scratch** | DJ-style varispeed scratch with direction control (back-forth, forward, backward, random) |
| **Stretch** | Granular time-stretch (pitch preserved) with Hann-windowed grains |
| **FreqShift** | Bode frequency shifter via 31-tap FIR Hilbert transform with LFO modulation |
| **Glitch** | Meta-effect — random effect selector combining all above effects with per-effect probability |

## Install

```bash
vivid packages install https://github.com/vivid-project/vivid-glitch
```

Or install from the in-app package browser: **Browse Packages > vivid-glitch > Install**.

## Usage

All glitch operators are audio effects with a `phase` input for beat sync and a `mix` control for dry/wet blending.

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

## License

MIT
