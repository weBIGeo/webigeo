/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#pragma once

#include "Node.h"

#include "webgpu_engine/Buffer.h"
#include "webgpu_engine/PipelineManager.h"

namespace webgpu_engine::compute::nodes {

class ComputeAvalancheTrajectoriesNode : public Node {
    Q_OBJECT

public:
    static glm::uvec3 SHADER_WORKGROUP_SIZE; // TODO currently hardcoded in shader! can we somehow not hardcode it? maybe using overrides

    enum PhysicsModelType : uint32_t {
        MODEL1 = 0,
        MODEL2 = 1,
        MODEL3 = 2,
        MODEL4 = 3,
        D8_NO_WEIGHTS = 4,
        D8_WEIGHTS = 5,
    };

    enum RunoutModelType : uint32_t {
        NONE = 0,
        PERLA = 1,
    };

    struct Model1Params {
        float slowdown_coefficient = 0.0033f;
        float speedup_coefficient = 0.12f;
    };

    struct Model2Params {
        float gravity = 9.81f;
        float mass = 10.0f;
        float friction_coeff = 0.01f;
        float drag_coeff = 0.2f;
    };

    struct ModelD8WithWeightsParams {
        std::array<float, 8> weights = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        float center_height_offset = 1.0f;
    };

    struct RunoutPerlaParams {
        float my = 0.11f; // sliding friction coeff
        float md = 40.0f; // M/D mass-to-drag ratio (in m)
        float l = 1.0f; // distance between grid cells (in m)
        float g = 9.81f; // acceleration due to gravity (in m/s^2)
    };

    struct AvalancheTrajectoriesSettings {
        uint32_t num_steps = 1024;
        float step_length = 0.1f;

        PhysicsModelType active_model;
        Model1Params model1;
        Model2Params model2;
        ModelD8WithWeightsParams model_d8_with_weights;

        RunoutModelType active_runout_model = RunoutModelType::PERLA;
        RunoutPerlaParams perla;
    };

private:
    struct AvalancheTrajectoriesSettingsUniform {
        glm::uvec2 output_resolution;
        glm::fvec2 region_size;

        uint32_t num_steps = 128;
        float step_length = 0.5f;

        PhysicsModelType physics_model_type;
        float model1_linear_drag_coeff;
        float model1_downward_acceleration_coeff;
        float model2_gravity;
        float model2_mass;
        float model2_friction_coeff;
        float model2_drag_coeff;
        float model_d8_with_weights_weights[8];
        float model_d8_with_weights_center_height_offset;

        RunoutModelType runout_model_type;

        float runout_perla_my;
        float runout_perla_md;
        float runout_perla_l;
        float runout_perla_g;

        uint32_t padding1;
        uint32_t padding2;
        uint32_t padding3;
        uint32_t padding4;
        uint32_t padding5;
    };

public:
    ComputeAvalancheTrajectoriesNode(const PipelineManager& pipeline_manager, WGPUDevice device);

    void update_gpu_settings();

    void set_settings(const AvalancheTrajectoriesSettings& settings) { m_settings = settings; }

public slots:
    void run_impl() override;

private:
    static std::unique_ptr<webgpu::raii::Sampler> create_sampler(WGPUDevice device);

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    AvalancheTrajectoriesSettings m_settings;
    webgpu_engine::Buffer<AvalancheTrajectoriesSettingsUniform> m_settings_uniform;
    std::unique_ptr<webgpu::raii::Sampler> m_sampler;
    std::unique_ptr<webgpu::raii::RawBuffer<uint32_t>> m_output_storage_buffer;
    glm::uvec2 m_output_dimensions;
};

} // namespace webgpu_engine::compute::nodes
