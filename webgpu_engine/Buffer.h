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

#include "webgpu.hpp"
#include <QString>

namespace webgpu_engine {

// Generic class for GPU buffer handles complying to RAII.
// This class does not store the written value on CPU side.
template <typename T> class RawBuffer {
public:
    // m_size in num objects
    RawBuffer(wgpu::Device device, wgpu::BufferUsageFlags usage, size_t size)
        : m_size(size)
    {
        wgpu::BufferDescriptor bufferDesc;
        bufferDesc.usage = usage;
        bufferDesc.size = size * sizeof(T); // takes size in bytes
        bufferDesc.mappedAtCreation = false;
        m_buffer = device.createBuffer(bufferDesc);
        // this->update_gpu_data();
    }

    // delete copy constructor and copy-assignment operator
    RawBuffer(const RawBuffer& other) = delete;
    RawBuffer& operator=(const RawBuffer& other) = delete;

    ~RawBuffer()
    {
        m_buffer.destroy();
        m_buffer.release();
    }

    void write(wgpu::Queue queue, const T* data, size_t count = 1, size_t offset = 0)
    {
        assert(count <= m_size);
        queue.writeBuffer(m_buffer, offset, data, count * sizeof(T)); // takes size in bytes
    }

    wgpu::Buffer handle() const { return m_buffer; }

private:
    wgpu::Buffer m_buffer = nullptr;
    size_t m_size;
};

// Generic class for buffers that are backed by a member variable
template <typename T> class Buffer {
public:
    // Creates a Buffer object representing a region in GPU memory.
    Buffer(wgpu::Device device, wgpu::BufferUsageFlags flags);

    // Refills the GPU Buffer
    void update_gpu_data(wgpu::Queue queue);

    // Returns String representation of buffer data (Base64)
    QString data_as_string();

    // Loads the given base 64 encoded string as the buffer data
    bool data_from_string(const QString& base64String);

    // Retrieves underlying buffer handle
    wgpu::Buffer handle() const;

public:
    // Contains the buffer data
    T data;

protected:
    RawBuffer<T> m_non_backed_buffer;
};

} // namespace webgpu_engine
