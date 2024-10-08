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
#include <webgpu/raii/base_types.h>

namespace webgpu_engine {

ShaderModuleManager::ShaderModuleManager(WGPUDevice device, const std::filesystem::path& prefix)
    : m_device(device)
    , m_prefix(prefix)
{}

void ShaderModuleManager::create_shader_modules()
{
    m_tile_shader_module = create_shader_module("Tile.wgsl");
    m_screen_pass_vert_shader_module = create_shader_module("screen_pass_vert.wgsl");
    m_compose_frag_shader_module = create_shader_module("compose_frag.wgsl");
    m_atmosphere_frag_shader_module = create_shader_module("atmosphere_frag.wgsl");
    m_normals_compute_module = create_shader_module("normals_compute.wgsl");
    m_snow_compute_module = create_shader_module("snow_compute.wgsl");
    m_downsample_compute_module = create_shader_module("downsample_compute.wgsl");
    m_upsample_textures_compute_module = create_shader_module("upsample_textures_compute.wgsl");
    m_line_render_module = create_shader_module("line_render.wgsl");
}

void ShaderModuleManager::release_shader_modules()
{
    m_tile_shader_module.release();
    m_screen_pass_vert_shader_module.release();
    m_compose_frag_shader_module.release();
    m_atmosphere_frag_shader_module.release();
    m_normals_compute_module.release();
    m_snow_compute_module.release();
    m_downsample_compute_module.release();
    m_upsample_textures_compute_module.release();
    m_line_render_module.release();
}

const webgpu::raii::ShaderModule& ShaderModuleManager::tile() const { return *m_tile_shader_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::screen_pass_vert() const { return *m_screen_pass_vert_shader_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::compose_frag() const { return *m_compose_frag_shader_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::atmosphere_frag() const { return *m_atmosphere_frag_shader_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::normals_compute() const { return *m_normals_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::snow_compute() const { return *m_snow_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::downsample_compute() const { return *m_downsample_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::upsample_textures_compute() const { return *m_upsample_textures_compute_module; }

const webgpu::raii::ShaderModule& ShaderModuleManager::line_render() const { return *m_line_render_module; }

std::string ShaderModuleManager::read_file_contents(const std::string& name) const {
    const auto path = m_prefix / name; // operator/ concats paths
    auto file = QFile(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "could not open shader file " << path << std::endl;
        throw std::runtime_error("could not open file " + path.string());
    }
    return file.readAll().toStdString();
}

std::string ShaderModuleManager::get_contents(const std::string& name) {
    const auto found_it = m_shader_name_to_code.find(name);
    if (found_it != m_shader_name_to_code.end()) {
        return found_it->second;
    }
    const auto file_contents = read_file_contents(name);
    const auto preprocessed_contents = preprocess(file_contents);
    m_shader_name_to_code[name] = preprocessed_contents;
    return preprocessed_contents;
}

std::unique_ptr<webgpu::raii::ShaderModule> ShaderModuleManager::create_shader_module(const std::string& filename)
{
    const std::string code = get_contents(filename);
    WGPUShaderModuleDescriptor shader_module_desc {};
    WGPUShaderModuleWGSLDescriptor wgsl_desc {};
    wgsl_desc.chain.next = nullptr;
    wgsl_desc.chain.sType = WGPUSType::WGPUSType_ShaderModuleWGSLDescriptor;
    wgsl_desc.code = code.data();
    shader_module_desc.label = filename.data();
    shader_module_desc.nextInChain = &wgsl_desc.chain;
    return std::make_unique<webgpu::raii::ShaderModule>(m_device, shader_module_desc);
}

std::unique_ptr<webgpu::raii::ShaderModule> ShaderModuleManager::create_shader_module(const std::string& name, const std::string& code)
{
    const std::string preprocessed_code = preprocess(code);
    WGPUShaderModuleDescriptor shader_module_desc {};
    WGPUShaderModuleWGSLDescriptor wgsl_desc {};
    wgsl_desc.chain.next = nullptr;
    wgsl_desc.chain.sType = WGPUSType::WGPUSType_ShaderModuleWGSLDescriptor;
    wgsl_desc.code = preprocessed_code.data();
    shader_module_desc.label = name.data();
    shader_module_desc.nextInChain = &wgsl_desc.chain;
    return std::make_unique<webgpu::raii::ShaderModule>(m_device, shader_module_desc);
}

std::string ShaderModuleManager::preprocess(const std::string& code)
{
    std::string preprocessed_code = code;
    const std::regex include_regex("#include \"([a-zA-Z0-9 ._-]+)\"");
    for (std::smatch include_match; std::regex_search(preprocessed_code, include_match, include_regex);) {
        const std::string included_filename = include_match[1].str(); // first submatch
        const std::string included_file_contents = get_contents(included_filename);
        preprocessed_code.replace(include_match.position(), include_match.length(), included_file_contents);
    }
    return preprocessed_code;
}

} // namespace webgpu_engine
