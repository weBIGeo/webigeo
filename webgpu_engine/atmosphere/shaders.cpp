/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#include "shaders.h"

#include "webgpu_engine/ShaderModuleManager.h"
#include <QString>

namespace webgpu_engine::atmosphere::shaders {

std::string make_phase_shader_code(std::optional<float> constDropletDiameter)
{
    QString code = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/phase.wgsl"));
    code.replace("// include hg_draine_const",
        QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/hg_draine_phase_const.wgsl")));
    if (!constDropletDiameter.has_value() || constDropletDiameter.value() >= 5.0f) {
        code.replace("// include hg_draine_size",
            QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/hg_draine_large.wgsl")));
    } else if (constDropletDiameter.value() >= 1.5) {
        code.replace("// include hg_draine_size",
            QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/hg_draine_mid2.wgsl")));
    } else if (constDropletDiameter.value() > 0.1) {
        code.replace("// include hg_draine_size",
            QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/hg_draine_mid1.wgsl")));
    } else {
        code.replace("// include hg_draine_size",
            QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/hg_draine_small.wgsl")));
    }
    return code.toStdString();
}

std::string make_shadow_shader_code(const std::string& shadow_code)
{
    QString shader_code = "";
    if (shadow_code.empty()) {
        shader_code.append("fn get_shadow(p: vec3<f32>, i: u32) -> f32 { return 1.0; }");
    } else {
        shader_code.append(shadow_code);
    }
    shader_code.append(QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/shadow_base.wgsl")));
    return shader_code.toStdString();
}

} // namespace webgpu_engine::atmosphere::shaders
