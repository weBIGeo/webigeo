/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include <optional>
#include <string>

namespace webgpu_engine::atmosphere::shaders {

std::string make_phase_shader_code(std::optional<float> constDropletDiameter = {});

std::string make_shadow_shader_code(const std::string& shadow_code = "");

} // namespace webgpu_engine::atmosphere::shaders
