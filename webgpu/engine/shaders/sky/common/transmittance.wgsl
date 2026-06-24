// Samples the transmittance LUT for the given view_height / cos_zenith pair.
// rho = sqrt(max(0, view_height^2 - bottom_radius^2))
// h   = sqrt(max(0, top_radius^2  - bottom_radius^2))
// Callers must declare: atmosphere (Atmosphere uniform), transmittance_lut, transmittance_sampler.
fn lookup_transmittance(view_height: f32, cos_zenith: f32, rho: f32, h: f32) -> vec3f {
    let discriminant = view_height * view_height * (cos_zenith * cos_zenith - 1.0) + atmosphere.top_radius * atmosphere.top_radius;
    let d    = max(0.0, -view_height * cos_zenith + sqrt(max(discriminant, 0.0)));
    let x_mu = (d - (atmosphere.top_radius - view_height)) / (rho + h);
    let x_r  = rho / h;
    return textureSampleLevel(transmittance_lut, transmittance_sampler, vec2f(x_mu, x_r), 0).rgb;
}
