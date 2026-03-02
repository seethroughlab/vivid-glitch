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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);

    let amount       = u.amount;
    let density      = u.density;
    let speed        = u.speed;
    let color_mode   = u.color_mode;
    let scanline_amt = u.scanline;
    let v_hold       = u.vertical_hold;
    let interf       = u.interference;
    let wet          = u.mix;

    // Time seed — quantized to speed (frames per second of noise change)
    let t = floor(u.time * speed);

    // Pixel coordinates scaled by density
    let px = input.uv * u.resolution * density / u.resolution.y;

    // Base white noise
    var noise: vec3f;
    let mode = i32(color_mode + 0.5);
    if (mode == 1) {
        // Color: independent RGB noise
        noise = vec3f(
            hash21(px + vec2f(t, 0.0)),
            hash21(px + vec2f(t, 17.31)),
            hash21(px + vec2f(t, 43.17))
        );
    } else if (mode == 2) {
        // Channel: one random channel per frame
        let ch = i32(hash11(t * 7.13) * 3.0);
        let n = hash21(px + vec2f(t, 0.0));
        if (ch == 0) {
            noise = vec3f(n, 0.0, 0.0);
        } else if (ch == 1) {
            noise = vec3f(0.0, n, 0.0);
        } else {
            noise = vec3f(0.0, 0.0, n);
        }
    } else {
        // Mono: grayscale noise
        let n = hash21(px + vec2f(t, 0.0));
        noise = vec3f(n);
    }

    var result = noise;

    // Scanline darkening
    if (scanline_amt > 0.0) {
        let sl = 0.5 + 0.5 * sin(input.uv.y * u.resolution.y * PI);
        result = result * mix(1.0, sl, scanline_amt);
    }

    // Diagonal interference pattern
    if (interf > 0.0) {
        let diag = sin((input.uv.x + input.uv.y) * u.resolution.y * 0.5 + u.time * 20.0);
        result = result + vec3f(diag * interf * 0.3);
    }

    // Vertical hold rolling
    var uv_shifted = input.uv;
    if (v_hold > 0.0) {
        uv_shifted.y = fract(input.uv.y + u.time * v_hold * 0.5);
    }
    let shifted_original = textureSample(inputTex, texSampler, uv_shifted);

    // Mix static with (possibly rolled) original
    let static_color = vec4f(
        mix(shifted_original.rgb, result, amount),
        shifted_original.a
    );

    return mix(original, static_color, wet);
}
