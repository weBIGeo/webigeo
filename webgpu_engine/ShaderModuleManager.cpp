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
#include "ShaderModuleManager.h"

#include <QFile>
#include <iostream>
#include <memory>
#include <regex>
#include <unordered_set>
#include <webgpu/raii/base_types.h>

namespace webgpu_engine {

ShaderModuleManager::ShaderModuleManager(WGPUDevice device)
    : m_device(device)
{}

void ShaderModuleManager::create_shader_modules()
{
    m_render_tiles_shader_module = create_shader_module_for_file("render_tiles.wgsl");
    m_render_atmosphere_shader_module = create_shader_module_for_file("render_atmosphere.wgsl");
    m_render_lines_module = create_shader_module_for_file("render_lines.wgsl");
    m_compose_pass_shader_module = create_shader_module_for_file("compose_pass.wgsl");

    m_normals_compute_module = create_shader_module_for_file("compute/normals_compute.wgsl");
    m_snow_compute_module = create_shader_module_for_file("compute/snow_compute.wgsl");
    m_downsample_compute_module = create_shader_module_for_file("compute/downsample_compute.wgsl");
    m_upsample_textures_compute_module = create_shader_module_for_file("compute/upsample_textures_compute.wgsl");
    m_avalanche_trajectories_compute_module = create_shader_module_for_file("compute/avalanche_trajectories_compute.wgsl");
    m_buffer_to_texture_compute_module = create_shader_module_for_file("compute/buffer_to_texture_compute.wgsl");
    m_avalanche_influence_area_compute_module = create_shader_module_for_file("compute/avalanche_influence_area_compute.wgsl");
    m_d8_compute_module = create_shader_module_for_file("compute/d8_compute.wgsl");
    m_release_point_compute_module = create_shader_module_for_file("compute/compute_release_points.wgsl");
}

void ShaderModuleManager::release_shader_modules()
{
    m_shader_name_to_code.clear();

    m_render_tiles_shader_module.release();
    m_compose_pass_shader_module.release();
    m_render_atmosphere_shader_module.release();
    m_render_lines_module.release();

    m_normals_compute_module.release();
    m_snow_compute_module.release();
    m_downsample_compute_module.release();
    m_upsample_textures_compute_module.release();
    m_avalanche_trajectories_compute_module.release();
    m_buffer_to_texture_compute_module.release();
    m_avalanche_influence_area_compute_module.release();
    m_d8_compute_module.release();
    m_release_point_compute_module.release();
}

const webgpu::raii::ShaderModule& ShaderModuleManager::render_tiles() const { return *m_render_tiles_shader_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::render_atmosphere() const { return *m_render_atmosphere_shader_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::render_lines() const { return *m_render_lines_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::compose_pass() const { return *m_compose_pass_shader_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::normals_compute() const { return *m_normals_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::snow_compute() const { return *m_snow_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::downsample_compute() const { return *m_downsample_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::upsample_textures_compute() const { return *m_upsample_textures_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::avalanche_trajectories_compute() const { return *m_avalanche_trajectories_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::buffer_to_texture_compute() const { return *m_buffer_to_texture_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::avalanche_influence_area_compute() const { return *m_avalanche_influence_area_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::d8_compute() const { return *m_d8_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::release_point_compute() const { return *m_release_point_compute_module; }

std::string ShaderModuleManager::load_and_preprocess_without_cache(const std::string& path)
{
    // TODO de-duplicate code
    std::string preprocessed_code = read_file_contents(path);
    std::regex include_regex("#include \"([/a-zA-Z0-9 ._-]+)\"");
    std::unordered_set<std::string> already_included {};
    size_t search_start_pos = 0;
    for (std::cmatch include_match {}; std::regex_search(preprocessed_code.c_str() + search_start_pos, include_match, include_regex);) {
        const std::string included_filename = include_match[1].str(); // first submatch
        if (already_included.contains(included_filename)) {
            preprocessed_code.replace(search_start_pos + include_match.position(), include_match.length(), "");
        } else {
            const std::string included_file_contents = read_file_contents(included_filename);
            preprocessed_code.replace(search_start_pos + include_match.position(), include_match.length(), included_file_contents);
        }
        search_start_pos += include_match.position();
        already_included.insert(included_filename);
    }
    return preprocessed_code;
}

std::unique_ptr<webgpu::raii::ShaderModule> ShaderModuleManager::create_shader_module(WGPUDevice device, const std::string& label, const std::string& code)
{
    WGPUShaderModuleDescriptor shader_module_desc {};
    WGPUShaderModuleWGSLDescriptor wgsl_desc {};
    wgsl_desc.chain.next = nullptr;
    wgsl_desc.chain.sType = WGPUSType::WGPUSType_ShaderModuleWGSLDescriptor;
    wgsl_desc.code = code.data();
    shader_module_desc.label = label.data();
    shader_module_desc.nextInChain = &wgsl_desc.chain;
    return std::make_unique<webgpu::raii::ShaderModule>(device, shader_module_desc);
}

std::string ShaderModuleManager::read_file_contents(const std::string& name)
{
#ifdef __EMSCRIPTEN__
    const auto path = QRC_PREFIX / name; // use qrc file prefix for emscripten builds
#else
    const auto path = LOCAL_PREFIX / name; // use external (local) file path for native builds
#endif
    auto file = QFile(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "could not open shader file " << path << std::endl;
        throw std::runtime_error("could not open file " + path.string());
    }
    return file.readAll().toStdString();
}

std::string ShaderModuleManager::get_file_contents_with_cache(const std::string& name)
{
    const auto found_it = m_shader_name_to_code.find(name);
    if (found_it != m_shader_name_to_code.end()) {
        return found_it->second;
    }
    const auto file_contents = read_file_contents(name);
    m_shader_name_to_code[name] = file_contents;
    return file_contents;
}

std::unique_ptr<webgpu::raii::ShaderModule> ShaderModuleManager::create_shader_module_for_file(const std::string& filename)
{
    const std::string code = preprocess(get_file_contents_with_cache(filename));
    return create_shader_module(m_device, filename, code);
}

std::string ShaderModuleManager::preprocess(const std::string& code)
{
    std::string preprocessed_code = code;
    std::regex include_regex("#include \"([/a-zA-Z0-9 ._-]+)\"");
    std::unordered_set<std::string> already_included {};
    size_t search_start_pos = 0;
    for (std::cmatch include_match {}; std::regex_search(preprocessed_code.c_str() + search_start_pos, include_match, include_regex);) {
        const std::string included_filename = include_match[1].str(); // first submatch
        if (already_included.contains(included_filename)) {
            preprocessed_code.replace(search_start_pos + include_match.position(), include_match.length(), "");
        } else {
            const std::string included_file_contents = get_file_contents_with_cache(included_filename);
            preprocessed_code.replace(search_start_pos + include_match.position(), include_match.length(), included_file_contents);
        }
        search_start_pos += include_match.position();
        already_included.insert(included_filename);
    }
    return preprocessed_code;
}

} // namespace webgpu_engine
