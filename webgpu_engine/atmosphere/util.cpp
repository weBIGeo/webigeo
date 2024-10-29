#include "util.h"

namespace webgpu_engine::atmosphere::util {

LookUpTable::LookUpTable(std::unique_ptr<webgpu::raii::Texture> texture)
    : m_texture { std::move(texture) }
    , m_view { m_texture->create_view() }
{
}

webgpu::raii::Texture& LookUpTable::texture() { return *m_texture; }
const webgpu::raii::Texture& LookUpTable::texture() const { return *m_texture; }

webgpu::raii::TextureView& LookUpTable::view() { return *m_view; }
const webgpu::raii::TextureView& LookUpTable::view() const { return *m_view; }

ComputePass::ComputePass(
    WGPUComputePipeline pipeline, std::vector<std::unique_ptr<webgpu::raii::BindGroup>>& bind_groups, const glm::uvec3& dispatch_dimensions)
    : m_pipeline { pipeline }
    , m_bind_groups {}
    , m_dispatch_dimensions { dispatch_dimensions }
{
    m_bind_groups.swap(bind_groups);
}

void ComputePass::encode(WGPUComputePassEncoder compute_pass, bool reset_bind_groups)
{
    wgpuComputePassEncoderSetPipeline(compute_pass, m_pipeline);
    for (uint32_t i = 0; i < m_bind_groups.size(); i++) {
        wgpuComputePassEncoderSetBindGroup(compute_pass, i, m_bind_groups.at(i)->handle(), 0, nullptr);
    }
    wgpuComputePassEncoderDispatchWorkgroups(compute_pass, m_dispatch_dimensions.x, m_dispatch_dimensions.y, m_dispatch_dimensions.z);
    if (reset_bind_groups) {
        for (uint32_t i = 0; i < m_bind_groups.size(); i++) {
            wgpuComputePassEncoderSetBindGroup(compute_pass, i, nullptr, 0, nullptr);
        }
    }
}

void ComputePass::replace_bind_group(uint32_t index, std::unique_ptr<webgpu::raii::BindGroup> bind_group) { m_bind_groups[index] = std::move(bind_group); }

void ComputePass::replace_dispatch_dimensions(const glm::uvec3& dispatch_dimensions) { m_dispatch_dimensions = dispatch_dimensions; }

std::unique_ptr<webgpu::raii::Sampler> makeLutSampler(WGPUDevice device)
{
    WGPUSamplerDescriptor descriptor {};
    descriptor.label = "LUT sampler";
    descriptor.addressModeU = WGPUAddressMode_ClampToEdge;
    descriptor.addressModeV = WGPUAddressMode_ClampToEdge;
    descriptor.addressModeW = WGPUAddressMode_ClampToEdge;
    descriptor.minFilter = WGPUFilterMode_Linear;
    descriptor.magFilter = WGPUFilterMode_Linear;
    descriptor.mipmapFilter = WGPUMipmapFilterMode_Linear;
    descriptor.lodMinClamp = 0;
    descriptor.lodMaxClamp = 32;
    descriptor.maxAnisotropy = 1;
    return std::make_unique<webgpu::raii::Sampler>(device, descriptor);
}

} // namespace webgpu_engine::atmosphere::util
