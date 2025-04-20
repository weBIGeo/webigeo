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
        PHYSICS_SIMPLE = 0,
        PHYSICS_LESS_SIMPLE = 1,
        GRADIENT = 2,
        DISCRETIZED_GRADIENT = 3,
        D8_NO_WEIGHTS = 4,
        D8_WEIGHTS = 5,
    };

    enum RunoutModelType : uint32_t {
        NONE = 0,
        PERLA = 1,
        FLOWPY = 2,
    };

    struct ModelPhysicsSimpleParams {
        float slowdown_coefficient = 0.0033f;
        float speedup_coefficient = 0.12f;
    };

    struct ModelPhysicsLessSimpleParams {
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

    /* FlowPy runout model */
    struct RunoutFlowPyParams {
        float alpha = glm::radians(25.0f);
    };

    struct OutputLayerParams {
        uint32_t layer1_zdelta_enabled = 1u;
        uint32_t layer2_cellCounts_enabled = 1u;
        uint32_t layer3_travelLength_enabled = 1u;
        uint32_t layer4_travelAngle_enabled = 1u;
    };

    struct AvalancheTrajectoriesSettings {
        uint32_t resolution_multiplier = 1;
        uint32_t num_steps = 1024;
        float step_length = 0.1f;

        /* Number of trajectories to start from the same release cell.
         * This only makes sense when random_contribution is greater than 0.*/
        uint32_t num_paths_per_release_cell = 1u;

        /* Randomness contribution to the sampled normal in [0,1].
           0 means no randomness, 1 means only randomness */
        float random_contribution = 0.2f;

        /* Persistence contribution to the (randomly offset) normal in [0,1].
           0 means only local normal, 1 means only last normal*/
        float persistence_contribution = 0.2f;

        PhysicsModelType active_model;
        ModelPhysicsSimpleParams model1;
        ModelPhysicsLessSimpleParams model2;
        ModelD8WithWeightsParams model_d8_with_weights;

        RunoutModelType active_runout_model = RunoutModelType::PERLA;
        RunoutPerlaParams runout_perla;
        RunoutFlowPyParams runout_flowpy;

        OutputLayerParams output_layer;
    };

private:
    struct AvalancheTrajectoriesSettingsUniform {
        glm::uvec2 output_resolution;
        glm::fvec2 region_size;

        uint32_t num_steps = 128;
        float step_length = 0.5f;

        float normal_offset = 0.2f;
        float direction_offset = 0.0f;

        PhysicsModelType physics_model_type;
        float model1_linear_drag_coeff;
        float model1_downward_acceleration_coeff;
        float model2_gravity;
        float model2_mass;
        float model2_friction_coeff;
        float model2_drag_coeff;
        float model_d8_with_weights_center_height_offset;
        float model_d8_with_weights_weights[8];

        RunoutModelType runout_model_type;

        float runout_perla_my;
        float runout_perla_md;
        float runout_perla_l;
        float runout_perla_g;

        float runout_flowpy_alpha;

        OutputLayerParams output_layer;

        uint32_t padding1;
        uint32_t padding2;
    };

public:
    ComputeAvalancheTrajectoriesNode(const PipelineManager& pipeline_manager, WGPUDevice device);
    ComputeAvalancheTrajectoriesNode(const PipelineManager& pipeline_manager, WGPUDevice device, const AvalancheTrajectoriesSettings& settings);

    void update_gpu_settings();

    void set_settings(const AvalancheTrajectoriesSettings& settings) { m_settings = settings; }

public slots:
    void run_impl() override;

private:
    static std::unique_ptr<webgpu::raii::Sampler> create_normal_sampler(WGPUDevice device);
    static std::unique_ptr<webgpu::raii::Sampler> create_height_sampler(WGPUDevice device);

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    AvalancheTrajectoriesSettings m_settings;
    webgpu_engine::Buffer<AvalancheTrajectoriesSettingsUniform> m_settings_uniform;
    std::unique_ptr<webgpu::raii::Sampler> m_normal_sampler;
    std::unique_ptr<webgpu::raii::Sampler> m_height_sampler;
    std::unique_ptr<webgpu::raii::RawBuffer<uint32_t>> m_output_storage_buffer;

    std::unique_ptr<webgpu::raii::RawBuffer<uint32_t>> m_layer1_zdelta_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<uint32_t>> m_layer2_cellCounts_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<uint32_t>> m_layer3_travelLength_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<uint32_t>> m_layer4_travelAngle_buffer;

    glm::uvec2 m_output_dimensions;
};

} // namespace webgpu_engine::compute::nodes
