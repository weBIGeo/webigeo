struct Settings {
    value_min: f32,
    value_max: f32,
    num_channels: u32,
    _pad: u32,
}

@group(0) @binding(0) var<uniform> settings: Settings;
@group(0) @binding(1) var input_texture: texture_2d<f32>;
@group(0) @binding(2) var output_texture: texture_storage_2d<rgba8unorm, write>;

fn norm(v: f32) -> f32 {
    return clamp((v - settings.value_min) / (settings.value_max - settings.value_min), 0.0, 1.0);
}

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let dims = textureDimensions(input_texture);
    if (id.x >= dims.x || id.y >= dims.y) { return; }

    let raw = textureLoad(input_texture, vec2i(id.xy), 0);
    var out: vec4<f32>;

    if (settings.num_channels == 1u) {
        let g = norm(raw.r);
        out = vec4f(g, g, g, 1.0);
    } else if (settings.num_channels == 2u) {
        out = vec4f(norm(raw.r), norm(raw.g), 0.0, 1.0);
    } else if (settings.num_channels == 3u) {
        out = vec4f(norm(raw.r), norm(raw.g), norm(raw.b), 1.0);
    } else {
        // 4 channels: preserve alpha as-is (already in [0,1] for unorm/float)
        out = vec4f(norm(raw.r), norm(raw.g), norm(raw.b), raw.a);
    }

    textureStore(output_texture, vec2i(id.xy), out);
}
