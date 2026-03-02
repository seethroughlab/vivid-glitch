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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);

    let amount     = u.amount;
    let freq       = u.frequency;
    let speed      = u.speed;
    let wave_mode  = i32(u.mode + 0.5);
    let bh         = u.band_height;
    let bc         = u.band_count;
    let jitter_amt = u.jitter;
    let wet        = u.mix;

    let t = u.time * speed;

    // Band mask: repeating bands that scroll vertically
    let band_phase = fract(input.uv.y * bc + t);
    let band_mask = smoothstep(0.0, 0.05, band_phase) *
                    (1.0 - smoothstep(bh - 0.05, bh, band_phase));

    // Scanline coordinate for displacement
    let scanline_y = input.uv.y * u.resolution.y;

    // Compute displacement based on waveform mode
    var displacement: f32;
    if (wave_mode == 0) {
        // Noise
        displacement = hash21(vec2f(floor(scanline_y), floor(t * 10.0))) * 2.0 - 1.0;
    } else if (wave_mode == 1) {
        // Sine
        displacement = sin(scanline_y / freq + t * TAU);
    } else if (wave_mode == 2) {
        // Square
        displacement = sign(sin(scanline_y / freq + t * TAU));
    } else {
        // Sawtooth
        displacement = fract(scanline_y / freq + t) * 2.0 - 1.0;
    }

    // Per-scanline jitter
    if (jitter_amt > 0.0) {
        let j = (hash11(scanline_y + t * 100.0) * 2.0 - 1.0) * jitter_amt;
        displacement = displacement + j;
    }

    // Apply horizontal offset
    let offset_x = displacement * amount * band_mask;
    let distorted_uv = vec2f(input.uv.x + offset_x, input.uv.y);

    let distorted = textureSample(inputTex, texSampler, distorted_uv);
    return mix(original, distorted, wet);
}
