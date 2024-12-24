//TODO copyright lukas herzberger!

@group(0) @binding(0) var input_texture: texture_2d<f32>;
@group(0) @binding(1) var output_texture: texture_storage_2d<bgra8unorm, write>; // trajectory tiles (output)

fn tonemap(rgb: vec3<f32>) -> vec3<f32> {
    let white_point = vec3(1.08241, 0.96756, 0.95003);
    let exposure = 10.0;
    return pow(vec3(1.0) - exp(-rgb / white_point * exposure), vec3(1.0 / 2.2));
}

fn sample_interleaved_gradient_noise(pixel_pos: vec2<f32>) -> f32 {
    let magic = vec3(0.06711056f, 0.00583715f, 52.9829189f);
    return fract(magic.z * fract(dot(pixel_pos.xy, magic.xy)));
}

// reduce banding: https://github.com/sebh/UnrealEngineSkyAtmosphere/issues/15
// TAA would be nicer
fn apply_dither_to_pixel_color(color: vec3<f32>, pixel_pos: vec2<f32>) -> vec3<f32> {
    let scale_bias = vec2(1.0 / 255.0, -0.5 / 255.0);
    return color + sample_interleaved_gradient_noise(pixel_pos) * scale_bias.x + scale_bias.y;
}

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    let output_texture_size = textureDimensions(output_texture);
    if (id.x >= output_texture_size.x || id.y >= output_texture_size.y) {
        return;
    }

    let input_color = textureLoad(input_texture, id.xy, 0).rgb;
    let output_color = apply_dither_to_pixel_color(tonemap(input_color), vec2f(id.xy));
    textureStore(output_texture, id.xy, vec4f(output_color, 1.0));
}