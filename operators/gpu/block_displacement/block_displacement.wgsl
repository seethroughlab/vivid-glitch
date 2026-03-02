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

    let amount    = u.amount;
    let gx        = u.grid_x;
    let gy        = u.grid_y;
    let chance    = u.chance;
    let dir       = i32(u.direction + 0.5);
    let dup       = u.duplicate;
    let seed      = u.seed;
    let wet       = u.mix;

    // Time-varying seed — pattern changes ~4x/second
    let t = floor(u.time * 4.0);

    // Block coordinates
    let grid = vec2f(gx, gy);
    let block_coord = floor(input.uv * grid);
    let block_seed = block_coord + vec2f(seed + t, seed * 0.7 + t * 1.3);

    // Per-block random values
    let block_rand = hash21(block_seed);
    let block_offset = hash22(block_seed + vec2f(71.0, 113.0)) * 2.0 - vec2f(1.0);

    // Should this block be displaced?
    if (block_rand > chance) {
        return mix(original, original, wet); // no displacement for this block
    }

    // Compute displacement
    var disp = block_offset * amount;

    // Direction constraint
    if (dir == 1) {
        disp.y = 0.0; // Horizontal only
    } else if (dir == 2) {
        disp.x = 0.0; // Vertical only
    }

    // Decide between offset vs duplication
    let dup_rand = hash21(block_seed + vec2f(200.0, 300.0));
    var sample_uv: vec2f;

    if (dup_rand < dup) {
        // Duplication: sample from a random other block
        let src_block = floor(hash22(block_seed + vec2f(500.0, 700.0)) * grid);
        let local_uv = fract(input.uv * grid);
        sample_uv = (src_block + local_uv) / grid;
    } else {
        // Simple displacement
        sample_uv = input.uv + disp;
    }

    let displaced = textureSample(inputTex, texSampler, sample_uv);
    return mix(original, displaced, wet);
}
