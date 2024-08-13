/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include <filesystem>
#include <map>
#include <string>
#include <webgpu/raii/base_types.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

class ShaderModuleManager {
public:
    const static inline std::filesystem::path DEFAULT_PREFIX = ":/wgsl_shaders/";

public:
    ShaderModuleManager(WGPUDevice device, const std::filesystem::path& prefix = DEFAULT_PREFIX);
    ~ShaderModuleManager() = default;

    void create_shader_modules();
    void release_shader_modules();

    const webgpu::raii::ShaderModule& tile() const;
    const webgpu::raii::ShaderModule& screen_pass_vert() const;
    const webgpu::raii::ShaderModule& compose_frag() const;
    const webgpu::raii::ShaderModule& atmosphere_frag() const;
    const webgpu::raii::ShaderModule& normals_compute() const;
    const webgpu::raii::ShaderModule& snow_compute() const;
    const webgpu::raii::ShaderModule& downsample_compute() const;
    const webgpu::raii::ShaderModule& upsample_textures_compute() const;

    std::unique_ptr<webgpu::raii::ShaderModule> create_shader_module(const std::string& name, const std::string& code);

private:
    std::string read_file_contents(const std::string& name) const;
    std::string get_contents(const std::string& name);
    std::string preprocess(const std::string& code);
    std::unique_ptr<webgpu::raii::ShaderModule> create_shader_module(const std::string& filename);

private:
    WGPUDevice m_device;
    std::filesystem::path m_prefix;

    std::map<std::string, std::string> m_shader_name_to_code;

    std::unique_ptr<webgpu::raii::ShaderModule> m_tile_shader_module;
    std::unique_ptr<webgpu::raii::ShaderModule> m_screen_pass_vert_shader_module;
    std::unique_ptr<webgpu::raii::ShaderModule> m_compose_frag_shader_module;
    std::unique_ptr<webgpu::raii::ShaderModule> m_atmosphere_frag_shader_module;
    std::unique_ptr<webgpu::raii::ShaderModule> m_normals_compute_module;
    std::unique_ptr<webgpu::raii::ShaderModule> m_snow_compute_module;
    std::unique_ptr<webgpu::raii::ShaderModule> m_downsample_compute_module;
    std::unique_ptr<webgpu::raii::ShaderModule> m_upsample_textures_compute_module;
};

} // namespace webgpu_engine
