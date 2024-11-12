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

    struct TriggerPoints {
        glm::uvec2 sampling_density;

        float min_steepness = 28.0f; // in degrees
        float max_steepness = 60.0f; // in degrees
    };

    enum PhysicsModelType : uint32_t {
        MODEL1 = 0,
        MODEL2 = 1,
        MODEL3 = 2,
        MODEL4 = 3,
        D8_NO_WEIGHTS = 4,
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

    struct Simulation {
        uint32_t zoomlevel = 16;
        uint32_t num_steps = 1024;
        float step_length = 0.1f;

        PhysicsModelType active_model;
        Model1Params model1;
        Model2Params model2;
    };

    struct AvalancheTrajectoriesSettings {
        TriggerPoints trigger_points;
        Simulation simulation;
    };

private:
    struct AvalancheTrajectoriesSettingsUniform {
        glm::uvec2 output_resolution;
        glm::uvec2 sampling_interval;

        uint32_t num_steps = 128;
        float step_length = 0.5f;
        uint32_t source_zoomlevel;

        PhysicsModelType physics_model_type;
        float model1_linear_drag_coeff;
        float model1_downward_acceleration_coeff;
        float model2_gravity;
        float model2_mass;
        float model2_friction_coeff;
        float model2_drag_coeff;

        float trigger_point_min_steepness;
        float trigger_point_max_steepness;
    };

public:
    ComputeAvalancheTrajectoriesNode(const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity);

    const GpuHashMap<tile::Id, uint32_t, GpuTileId>& hash_map() const { return m_output_tile_map; }
    GpuHashMap<tile::Id, uint32_t, GpuTileId>& hash_map() { return m_output_tile_map; }

    void update_gpu_settings();

    void set_area_of_influence_settings(const AvalancheTrajectoriesSettings& settings) { m_settings = settings; }
    const AvalancheTrajectoriesSettings& get_area_of_influence_settings() const { return m_settings; }

public slots:
    void run_impl() override;

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;
    size_t m_capacity;
    glm::uvec2 m_output_resolution;


    AvalancheTrajectoriesSettings m_settings;

    // calculated on cpu-side before each invocation
    webgpu::raii::RawBuffer<glm::vec4> m_tile_bounds; // aabb per tile

    // input
    webgpu::raii::RawBuffer<GpuTileId> m_input_tile_ids; // tile ids for which to calculate overlays
    webgpu_engine::Buffer<AvalancheTrajectoriesSettingsUniform> m_settings_uniform; // settings for area of influence calculation

    // output
    GpuHashMap<tile::Id, uint32_t, GpuTileId> m_output_tile_map; // hash map
    webgpu::raii::RawBuffer<uint32_t> m_output_storage_buffer; // storage buffer region per tile
};

class ComputeAvalancheTrajectoriesBufferToTextureNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex {
        TILE_ID_LIST_TO_PROCESS = 0,
        TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 1,
        INPUT_STORAGE_BUFFER = 2,
    };
    enum Output : SocketIndex {
        OUTPUT_TEXTURE_ARRAY = 0,
    };

    static glm::uvec3 SHADER_WORKGROUP_SIZE; // TODO currently hardcoded in shader! can we somehow not hardcode it? maybe using overrides

    ComputeAvalancheTrajectoriesBufferToTextureNode(
        const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity, WGPUTextureFormat output_format);

    const TileStorageTexture& texture_storage() const { return m_output_texture; }
    TileStorageTexture& texture_storage() { return m_output_texture; }

public slots:
    void run_impl() override;

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    // input
    webgpu::raii::RawBuffer<GpuTileId> m_input_tile_ids;

    // output
    TileStorageTexture m_output_texture; // texture per tile
};

} // namespace webgpu_engine::compute::nodes
