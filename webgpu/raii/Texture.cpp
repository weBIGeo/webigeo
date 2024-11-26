/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "Texture.h"
#include <QDebug>

namespace webgpu::raii {

uint8_t Texture::get_bytes_per_element(WGPUTextureFormat format)
{
    switch (format) {
        // 8-bit formats
    case WGPUTextureFormat_R8Unorm:
    case WGPUTextureFormat_R8Snorm:
    case WGPUTextureFormat_R8Uint:
    case WGPUTextureFormat_R8Sint:
        return 1;

        // 16-bit formats
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_R16Float:
    case WGPUTextureFormat_RG8Unorm:
    case WGPUTextureFormat_RG8Snorm:
    case WGPUTextureFormat_RG8Uint:
    case WGPUTextureFormat_RG8Sint:
        return 2;

        // 32-bit formats
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_R32Float:
    case WGPUTextureFormat_RG16Uint:
    case WGPUTextureFormat_RG16Sint:
    case WGPUTextureFormat_RG16Float:
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_RGBA8Snorm:
    case WGPUTextureFormat_RGBA8Uint:
    case WGPUTextureFormat_RGBA8Sint:
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
        // Packed 32-bit formats
    case WGPUTextureFormat_RGB9E5Ufloat:
    case WGPUTextureFormat_RGB10A2Uint:
    case WGPUTextureFormat_RGB10A2Unorm:
    case WGPUTextureFormat_RG11B10Ufloat:
        return 4;

        // 64-bit formats
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RG32Float:
    case WGPUTextureFormat_RGBA16Uint:
    case WGPUTextureFormat_RGBA16Sint:
    case WGPUTextureFormat_RGBA16Float:
        return 8;

        // 128-bit formats
    case WGPUTextureFormat_RGBA32Uint:
    case WGPUTextureFormat_RGBA32Sint:
    case WGPUTextureFormat_RGBA32Float:
        return 16;

    default:
        qFatal("tried to get texture size for unsupoorted format");
        return 0;
    }
}

const uint16_t Texture::BYTES_PER_ROW_PADDING = 256u;

void Texture::write(WGPUQueue queue, const nucleus::utils::ColourTexture& data, uint32_t layer)
{
    assert(static_cast<uint32_t>(data.width()) == m_descriptor.size.width);
    assert(static_cast<uint32_t>(data.height()) == m_descriptor.size.height);
    assert(data.format() == nucleus::utils::ColourTexture::Format::Uncompressed_RGBA); // TODO compressed textures

    WGPUImageCopyTexture image_copy_texture {};
    image_copy_texture.texture = m_handle;
    image_copy_texture.aspect = WGPUTextureAspect::WGPUTextureAspect_All;
    image_copy_texture.mipLevel = 0;
    image_copy_texture.origin = { 0, 0, layer };

    WGPUTextureDataLayout texture_data_layout {};
    texture_data_layout.bytesPerRow = 4 * data.width(); // for uncompressed RGBA
    texture_data_layout.rowsPerImage = data.height();
    texture_data_layout.offset = 0;
    WGPUExtent3D copy_extent { m_descriptor.size.width, m_descriptor.size.height, 1 };
    wgpuQueueWriteTexture(queue, &image_copy_texture, data.data(), data.n_bytes(), &texture_data_layout, &copy_extent);
}

void Texture::copy_to_texture(WGPUCommandEncoder encoder, uint32_t source_layer, const Texture& target_texture, uint32_t target_layer) const
{
    WGPUImageCopyTexture source {};
    source.texture = m_handle;
    source.mipLevel = 0;
    source.origin = { .x = 0, .y = 0, .z = source_layer };
    source.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture destination {};
    destination.texture = target_texture.handle();
    destination.mipLevel = 0;
    destination.origin = { .x = 0, .y = 0, .z = target_layer };
    destination.aspect = WGPUTextureAspect_All;

    const WGPUExtent3D extent { .width = m_descriptor.size.width, .height = m_descriptor.size.height, .depthOrArrayLayers = 1 };
    wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &extent);
}

void Texture::read_back_async(WGPUDevice device, size_t layer_index, ReadBackCallback callback)
{
    // create buffer and add buffer and callback to back of queue
    m_read_back_states.emplace(std::make_unique<raii::RawBuffer<char>>(
                                   device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, single_layer_size_in_bytes(), "texture read back staging buffer"),
        callback, layer_index);

    copy_to_buffer(device, *m_read_back_states.back().buffer, glm::uvec3(0, 0, uint32_t(layer_index)));

    auto on_buffer_mapped = [](WGPUBufferMapAsyncStatus status, void* user_data) {
        Texture* _this = reinterpret_cast<Texture*>(user_data);

        if (status != WGPUBufferMapAsyncStatus_Success) {
            qCritical() << "error: failed mapping buffer for ComputeTileStorage read back";
            _this->m_read_back_states.pop();
            return;
        }

        const ReadBackState& current_state = _this->m_read_back_states.front();

        const char* buffer_data = (const char*)wgpuBufferGetConstMappedRange(current_state.buffer->handle(), 0, current_state.buffer->size_in_byte());
        auto array = std::make_shared<QByteArray>();
        for (uint32_t i = 0; i < _this->m_descriptor.size.height; i++) {
            array->append(&buffer_data[i * _this->bytes_per_row()], _this->m_descriptor.size.width * get_bytes_per_element(_this->m_descriptor.format));
        }

        current_state.callback(current_state.layer_index, array);
        wgpuBufferUnmap(current_state.buffer->handle());

        _this->m_read_back_states.pop();
    };

    wgpuBufferMapAsync(
        m_read_back_states.back().buffer->handle(), WGPUMapMode_Read, 0, uint32_t(m_read_back_states.back().buffer->size_in_byte()), on_buffer_mapped, this);
}

WGPUTextureViewDescriptor Texture::default_texture_view_descriptor() const
{
    // TODO make utility function
    auto determineViewDimension = [](const WGPUTextureDescriptor& texture_desc) {
        if (texture_desc.dimension == WGPUTextureDimension_1D)
            return WGPUTextureViewDimension_1D;
        else if (texture_desc.dimension == WGPUTextureDimension_3D)
            return WGPUTextureViewDimension_3D;
        else if (texture_desc.dimension == WGPUTextureDimension_2D) {
            return texture_desc.size.depthOrArrayLayers > 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
            // note: if texture_desc.size.depthOrArrayLayers is 6, the view type can also be WGPUTextureViewDimension_Cube
            //       or, for any multiple of 6 the view type could be WGPUTextureViewDimension_CubeArray - we don't support this here for now
        }
        return WGPUTextureViewDimension_Undefined; // hopefully this logs an error when webgpu is validating
    };

    WGPUTextureViewDescriptor view_desc {};
    view_desc.aspect = WGPUTextureAspect_All;
    view_desc.dimension = determineViewDimension(m_descriptor);
    view_desc.format = m_descriptor.format;
    view_desc.baseArrayLayer = 0;
    // arrayLayerCount must be 1 for 3d textures, webGPU does not (yet) support 3d texture arrays
    view_desc.arrayLayerCount = m_descriptor.dimension == WGPUTextureDimension_3D ? 1u : m_descriptor.size.depthOrArrayLayers;
    view_desc.baseMipLevel = 0;
    view_desc.mipLevelCount = m_descriptor.mipLevelCount;
    return view_desc;
}

std::unique_ptr<TextureView> Texture::create_view() const { return create_view(default_texture_view_descriptor()); }

std::unique_ptr<TextureView> Texture::create_view(const WGPUTextureViewDescriptor& desc) const { return std::make_unique<TextureView>(m_handle, desc); }

size_t Texture::width() const { return m_descriptor.size.width; }

size_t Texture::height() const { return m_descriptor.size.height; }

size_t Texture::depth_or_num_layers() const { return m_descriptor.size.depthOrArrayLayers; }

size_t Texture::size_in_bytes() const { return single_layer_size_in_bytes() * m_descriptor.size.depthOrArrayLayers; }

size_t Texture::bytes_per_row() const
{
    return size_t(std::ceil(double(m_descriptor.size.width) * double(get_bytes_per_element(m_descriptor.format)) / double(BYTES_PER_ROW_PADDING))
        * BYTES_PER_ROW_PADDING); // rows are padded to 256 bytes
}

size_t Texture::single_layer_size_in_bytes() const { return bytes_per_row() * m_descriptor.size.height; }

} // namespace webgpu::raii
