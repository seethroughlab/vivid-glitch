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

fn luminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);

    let lo       = u.threshold_lo;
    let hi       = u.threshold_hi;
    let amount   = u.amount;
    let dir      = i32(u.direction + 0.5);
    let rev      = u.reverse;
    let smooth_a = u.smoothing;
    let seed     = u.seed;
    let wet      = u.mix;

    // Compute luminance of current pixel
    let luma = luminance(original.rgb);

    // Check if pixel is in the sorting threshold range
    let in_range = smoothstep(lo - smooth_a * 0.1, lo, luma) *
                   (1.0 - smoothstep(hi, hi + smooth_a * 0.1, luma));

    if (in_range < 0.01) {
        return original;
    }

    // Sort direction vector
    var sort_dir: vec2f;
    if (dir == 0) {
        sort_dir = vec2f(1.0, 0.0); // Horizontal
    } else if (dir == 1) {
        sort_dir = vec2f(0.0, 1.0); // Vertical
    } else {
        sort_dir = normalize(vec2f(1.0, 1.0)); // Diagonal
    }

    // Normalized luminance position within threshold range
    let range_pos = clamp((luma - lo) / max(hi - lo, 0.001), 0.0, 1.0);

    // Per-row/column random variation for organic feel
    let row_id = select(
        floor(input.uv.y * u.resolution.y),
        floor(input.uv.x * u.resolution.x),
        dir == 1
    );
    let row_rand = hash11(row_id + seed + floor(u.time * 2.0));
    let variation = (row_rand - 0.5) * 0.3;

    // Displacement: pixels shift along sort direction based on luminance rank
    var disp_amount = (range_pos + variation) * amount;

    // Reverse direction option
    if (rev > 0.5) {
        disp_amount = -disp_amount;
    }

    let pixel_size = 1.0 / u.resolution;
    let max_disp = 80.0; // Max pixel displacement
    let offset = sort_dir * disp_amount * max_disp * pixel_size;

    let sample_uv = input.uv + offset;
    let sorted = textureSample(inputTex, texSampler, sample_uv);

    // Blend based on range membership and wet/dry
    let result = mix(original, sorted, in_range * wet);
    return result;
}
