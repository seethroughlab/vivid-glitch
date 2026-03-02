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

fn rgb_to_yuv(c: vec3f) -> vec3f {
    let y = dot(c, vec3f(0.299, 0.587, 0.114));
    let cb = (c.b - y) * 0.565;
    let cr = (c.r - y) * 0.713;
    return vec3f(y, cb, cr);
}

fn yuv_to_rgb(yuv: vec3f) -> vec3f {
    let y = yuv.x;
    let cb = yuv.y;
    let cr = yuv.z;
    return vec3f(
        y + 1.403 * cr,
        y - 0.344 * cb - 0.714 * cr,
        y + 1.770 * cb
    );
}

fn luminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);

    let tracking_amt = u.tracking;
    let bleed_amt    = u.color_bleed;
    let noise_amt    = u.noise;
    let head_sw      = u.head_switch;
    let sharpen_amt  = u.luma_sharpen;
    let wobble_amt   = u.wobble;
    let spd          = u.speed;
    let wet          = u.mix;

    let t = u.time * spd;
    let px = 1.0 / u.resolution;
    var uv = input.uv;

    // --- 1. Vertical wobble ---
    if (wobble_amt > 0.0) {
        let wobble_offset = sin(uv.y * 3.0 + t * 2.0) * wobble_amt * 0.005
                          + sin(uv.y * 7.0 + t * 3.7) * wobble_amt * 0.002;
        uv.x = uv.x + wobble_offset;
    }

    // --- 2. Tracking errors: per-scanline X offset ---
    var tracking_line = 0.0;
    if (tracking_amt > 0.0) {
        let scanline = floor(uv.y * u.resolution.y);
        let line_seed = vec2f(scanline, floor(t * 8.0));
        let line_rand = hash21(line_seed);

        // Sporadic tracking bands
        let band_phase = fract(uv.y * 3.0 + t * 0.5);
        let band_mask = smoothstep(0.0, 0.02, band_phase) *
                        (1.0 - smoothstep(0.05, 0.07, band_phase));

        if (line_rand > (1.0 - tracking_amt * 0.3) && band_mask > 0.0) {
            let offset = (line_rand - 0.5) * tracking_amt * 0.1;
            uv.x = uv.x + offset;
            tracking_line = 1.0;
        }
    }

    // --- 3. Head switching: large offset in bottom 10-15% of frame ---
    if (head_sw > 0.0) {
        let head_zone = smoothstep(0.85, 0.88, uv.y) * (1.0 - smoothstep(0.95, 1.0, uv.y));
        if (head_zone > 0.0) {
            let head_rand = hash11(floor(t * 4.0) + 99.0);
            let head_offset = (head_rand - 0.5) * head_sw * 0.2;
            uv.x = uv.x + head_offset * head_zone;
            tracking_line = max(tracking_line, head_zone);
        }
    }

    // Sample with displaced UVs
    var color = textureSample(inputTex, texSampler, uv).rgb;

    // --- 4. Color bleed: keep sharp luma, blur chroma horizontally ---
    if (bleed_amt > 0.0) {
        let yuv_center = rgb_to_yuv(color);
        let sharp_luma = yuv_center.x;

        // Horizontal chroma blur (7-tap approximation for performance)
        let blur_width = bleed_amt * 8.0;
        var cb_sum = 0.0;
        var cr_sum = 0.0;
        var weight_sum = 0.0;
        let taps = 7;

        for (var i = -3; i <= 3; i++) {
            let sample_uv = vec2f(uv.x + f32(i) * px.x * blur_width, uv.y);
            let s = textureSample(inputTex, texSampler, sample_uv).rgb;
            let yuv_s = rgb_to_yuv(s);
            let w = 1.0 - abs(f32(i)) / 4.0;
            cb_sum = cb_sum + yuv_s.y * w;
            cr_sum = cr_sum + yuv_s.z * w;
            weight_sum = weight_sum + w;
        }

        let blurred_yuv = vec3f(sharp_luma, cb_sum / weight_sum, cr_sum / weight_sum);
        color = yuv_to_rgb(blurred_yuv);
    }

    // --- 5. Luma sharpen (unsharp mask) ---
    if (sharpen_amt > 0.0) {
        let luma_c = luminance(color);
        let luma_l = luminance(textureSample(inputTex, texSampler, uv + vec2f(-px.x, 0.0)).rgb);
        let luma_r = luminance(textureSample(inputTex, texSampler, uv + vec2f(px.x, 0.0)).rgb);
        let luma_u = luminance(textureSample(inputTex, texSampler, uv + vec2f(0.0, -px.y)).rgb);
        let luma_d = luminance(textureSample(inputTex, texSampler, uv + vec2f(0.0, px.y)).rgb);
        let luma_blur = (luma_l + luma_r + luma_u + luma_d) * 0.25;
        let sharpen = (luma_c - luma_blur) * sharpen_amt * 2.0;
        color = color + vec3f(sharpen);
    }

    // --- 6. Tape noise ---
    if (noise_amt > 0.0) {
        let noise_seed = vec2f(input.uv * u.resolution + vec2f(floor(t * 30.0)));
        var grain = (hash21(noise_seed) - 0.5) * noise_amt * 0.15;

        // Stronger noise on tracking error lines
        grain = grain * (1.0 + tracking_line * 3.0);

        color = color + vec3f(grain);
    }

    // Clamp output
    color = clamp(color, vec3f(0.0), vec3f(1.0));

    let result = vec4f(color, original.a);
    return mix(original, result, wet);
}
