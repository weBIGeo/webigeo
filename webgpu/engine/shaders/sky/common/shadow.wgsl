/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// Default (no-op) shadow implementation: nothing is in shadow.
// Mirrors the upstream port's make_shadow_shader_code("") path.
fn get_shadow(p: vec3<f32>, i: u32) -> f32 { return 1.0; }

///use webgpu_engine::sky/common/shadow_base
