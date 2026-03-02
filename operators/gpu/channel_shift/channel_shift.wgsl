fn apply_wrap(uv: vec2f, mode: i32) -> vec2f {
    if (mode == 1) {
        // Repeat
        return fract(uv);
    } else if (mode == 2) {
        // Mirror
        let m = abs(uv % 2.0);
        return vec2f(
            select(m.x, 2.0 - m.x, m.x > 1.0),
            select(m.y, 2.0 - m.y, m.y > 1.0)
        );
    }
    // Clamp (default — sampler handles this)
    return clamp(uv, vec2f(0.0), vec2f(1.0));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);

    let animate_on = u.animate;
    let anim_speed = u.animate_speed;
    let wrap_mode  = i32(u.wrap + 0.5);
    let wet        = u.mix;

    // Base offsets from params
    var r_off = vec2f(u.r_offset_x, u.r_offset_y);
    var g_off = vec2f(u.g_offset_x, u.g_offset_y);
    var b_off = vec2f(u.b_offset_x, u.b_offset_y);

    // Animation: modulate with sine waves at 120-degree phase offsets
    if (animate_on > 0.0) {
        let t = u.time * anim_speed;
        let phase_r = sin(t * TAU);
        let phase_g = sin(t * TAU + TAU / 3.0);
        let phase_b = sin(t * TAU + TAU * 2.0 / 3.0);
        r_off = r_off * mix(1.0, phase_r, animate_on);
        g_off = g_off * mix(1.0, phase_g, animate_on);
        b_off = b_off * mix(1.0, phase_b, animate_on);
    }

    // Sample each channel at offset UV
    let r_uv = apply_wrap(input.uv + r_off, wrap_mode);
    let g_uv = apply_wrap(input.uv + g_off, wrap_mode);
    let b_uv = apply_wrap(input.uv + b_off, wrap_mode);

    let r = textureSample(inputTex, texSampler, r_uv).r;
    let g = textureSample(inputTex, texSampler, g_uv).g;
    let b = textureSample(inputTex, texSampler, b_uv).b;

    let shifted = vec4f(r, g, b, original.a);
    return mix(original, shifted, wet);
}
