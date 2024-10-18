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
    };

    struct AvalancheTrajectoriesSettings {
        glm::uvec2 output_resolution;
        glm::uvec2 sampling_density;

        glm::vec4 target_point;
        glm::vec4 reference_point;
        uint32_t num_steps = 128;
        float step_length = 0.5;
        float radius = 20.0f;
        uint32_t source_zoomlevel;

        PhysicsModelType physics_model_type;
        float model1_linear_drag_coeff;
        float model1_downward_acceleration_coeff;
        float model2_gravity;
        float model2_mass;
        float model2_friction_coeff;
        float model2_drag_coeff;

        float trigger_point_max_steepness;
    };

    ComputeAvalancheTrajectoriesNode(const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity);

    const GpuHashMap<tile::Id, uint32_t, GpuTileId>& hash_map() const { return m_output_tile_map; }
    GpuHashMap<tile::Id, uint32_t, GpuTileId>& hash_map() { return m_output_tile_map; }

    void set_area_of_influence_settings(const AvalancheTrajectoriesSettings& settings) { m_input_settings.data = settings; }
    const AvalancheTrajectoriesSettings& get_area_of_influence_settings() const { return m_input_settings.data; }

    void set_target_point_lat_lon(const glm::dvec2& target_point_lat_lon);
    void set_target_point_world(const glm::dvec2& target_point_world);
    void set_reference_point_lat_lon_alt(const glm::dvec3& reference_point_lat_lon_alt);
    void set_reference_point_world(const glm::dvec3& reference_point_world);
    void set_num_steps(uint32_t num_steps) { m_input_settings.data.num_steps = num_steps; }
    void set_step_length(float step_length) { m_input_settings.data.step_length = step_length; }
    void set_radius(float radius);
    void set_source_zoomlevel(uint32_t source_zoomlevel) { m_input_settings.data.source_zoomlevel = source_zoomlevel; }
    void set_sampling_density(glm::uvec2 sampling_density) { m_input_settings.data.sampling_density = sampling_density; }
    void set_trigger_point_max_steepness(float trigger_point_max_steepness) { m_input_settings.data.trigger_point_max_steepness = trigger_point_max_steepness; }

    void set_physics_model_type(PhysicsModelType physics_model_type) { this->m_input_settings.data.physics_model_type = physics_model_type; }
    void set_model1_linear_drag_coeff(float model1_linear_drag_coeff) { this->m_input_settings.data.model1_linear_drag_coeff = model1_linear_drag_coeff; }
    void set_model1_downward_acceleration_coeff(float model1_downward_acceleration_coeff)
    {
        this->m_input_settings.data.model1_downward_acceleration_coeff = model1_downward_acceleration_coeff;
    }

    void set_model2_gravity(float model2_gravity) { this->m_input_settings.data.model2_gravity = model2_gravity; }
    void set_model2_mass(float model2_mass) { this->m_input_settings.data.model2_mass = model2_mass; }
    void set_model2_friction_coeff(float model2_friction_coeff) { this->m_input_settings.data.model2_friction_coeff = model2_friction_coeff; }
    void set_model2_drag_coeff(float model2_drag_coeff) { this->m_input_settings.data.model2_drag_coeff = model2_drag_coeff; }

    const glm::dvec3& get_reference_point_world() const { return m_reference_point; }
    const glm::dvec2& get_target_point_world() const { return m_target_point; }

public slots:
    void run_impl() override;

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;
    size_t m_capacity;
    glm::uvec2 m_output_resolution;

    bool m_should_output_files;

    glm::dvec2 m_target_point;
    glm::dvec3 m_reference_point;

    // calculated on cpu-side before each invocation
    webgpu::raii::RawBuffer<glm::vec4> m_tile_bounds; // aabb per tile

    // input
    webgpu::raii::RawBuffer<GpuTileId> m_input_tile_ids; // tile ids for which to calculate overlays
    webgpu_engine::Buffer<AvalancheTrajectoriesSettings> m_input_settings; // settings for area of influence calculation

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
