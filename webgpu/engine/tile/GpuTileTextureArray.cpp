/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

#include "webgpu/engine/tile/GpuTileTextureArray.h"

#include <utility>

namespace webgpu_engine {

GpuTileTextureArray::GpuTileTextureArray(uint32_t resolution, WGPUTextureFormat format, const WGPUSamplerDescriptor& sampler_desc, std::string label)
    : m_resolution { resolution }
    , m_format { format }
    , m_sampler_desc { sampler_desc }
    , m_label { std::move(label) }
{
}

void GpuTileTextureArray::init(webgpu::Context& ctx)
{
    m_ctx = &ctx;

    WGPUTextureDescriptor texture_desc {};
    texture_desc.label = WGPUStringView { .data = m_label.c_str(), .length = WGPU_STRLEN };
    texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    // TODO: array layers might become larger than allowed by the graphics API
    texture_desc.size = { m_resolution, m_resolution, capacity() };
    texture_desc.mipLevelCount = 1;
    texture_desc.sampleCount = 1;
    texture_desc.format = m_format;
    texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    m_texture = std::make_unique<webgpu::raii::TextureWithSampler>(ctx.device(), texture_desc, m_sampler_desc);
}

void GpuTileTextureArray::set_tile_limit(unsigned new_limit) { m_helper.set_tile_limit(new_limit); }

unsigned GpuTileTextureArray::add_tile(const nucleus::tile::Id& tile_id) { return m_helper.add_tile(tile_id); }

void GpuTileTextureArray::remove_tile(const nucleus::tile::Id& tile_id) { m_helper.remove_tile(tile_id); }

nucleus::tile::GpuArrayHelper::LayerInfo GpuTileTextureArray::layer(nucleus::tile::Id tile_id) const { return m_helper.layer(tile_id); }

bool GpuTileTextureArray::contains(nucleus::tile::Id tile_id) const { return m_helper.contains(tile_id); }

unsigned GpuTileTextureArray::capacity() const { return m_helper.size(); }

nucleus::tile::GpuArrayHelper::Dictionary GpuTileTextureArray::generate_dictionary() const { return m_helper.generate_dictionary(); }

} // namespace webgpu_engine
