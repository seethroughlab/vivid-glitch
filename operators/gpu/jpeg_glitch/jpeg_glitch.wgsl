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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);

    let amount     = u.amount;
    let bsize      = u.block_size;
    let corruption = u.corruption;
    let quant      = u.quantize;
    let ch_off     = u.channel_offset;
    let seed       = u.seed;
    let wet        = u.mix;

    // Block coordinates
    let pixel_pos = input.uv * u.resolution;
    let block_coord = floor(pixel_pos / bsize);
    let block_uv_center = (block_coord + 0.5) * bsize / u.resolution;

    // Time-varying seed for per-block randomness
    let t = floor(u.time * 2.0);
    let block_seed = block_coord + vec2f(seed + t, seed * 1.3 + t * 0.7);

    // Per-block random: corruption chance
    let block_rand = hash21(block_seed);

    // If this block isn't corrupted, return original (possibly quantized)
    if (block_rand > corruption) {
        // Light quantization on uncorrupted blocks based on amount
        if (quant > 0.0 && amount > 0.0) {
            let levels = mix(256.0, 8.0, quant * amount * 0.3);
            let q_color = floor(original.rgb * levels) / levels;
            return mix(original, vec4f(q_color, original.a), wet);
        }
        return original;
    }

    // --- Corrupted block effects ---

    // Block displacement: offset entire block sampling
    let disp_hash = hash22(block_seed + vec2f(100.0, 200.0));
    let block_disp = (disp_hash - 0.5) * amount * bsize * 2.0 / u.resolution;

    // Mosaic effect: sample from block center (simulates DCT zeroing)
    let mosaic_mix = hash21(block_seed + vec2f(300.0, 0.0)) * amount;
    let displaced_uv = input.uv + block_disp * amount;
    let displaced_color = textureSample(inputTex, texSampler, displaced_uv);
    let center_color = textureSample(inputTex, texSampler, block_uv_center);
    var color = mix(displaced_color.rgb, center_color.rgb, mosaic_mix);

    // Per-channel UV offset (R/G/B from slightly different positions)
    if (ch_off > 0.0) {
        let ch_seed = block_seed + vec2f(400.0, 500.0);
        let r_off = (hash22(ch_seed) - 0.5) * ch_off * bsize / u.resolution;
        let g_off = (hash22(ch_seed + vec2f(10.0, 20.0)) - 0.5) * ch_off * bsize / u.resolution;
        let b_off = (hash22(ch_seed + vec2f(30.0, 40.0)) - 0.5) * ch_off * bsize / u.resolution;

        let r = textureSample(inputTex, texSampler, input.uv + r_off).r;
        let g = textureSample(inputTex, texSampler, input.uv + g_off).g;
        let b = textureSample(inputTex, texSampler, input.uv + b_off).b;

        color = mix(color, vec3f(r, g, b), ch_off * amount);
    }

    // Quantization (simulate low-quality JPEG banding)
    if (quant > 0.0) {
        let levels = mix(64.0, 4.0, quant);
        color = floor(color * levels) / levels;
    }

    let glitched = vec4f(color, original.a);
    return mix(original, glitched, wet);
}
