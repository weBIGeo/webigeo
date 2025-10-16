/*****************************************************************************
 * Alpine Renderer
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

#include "UnittestWebgpuContext.h"
#include "webgpu/webgpu_interface.hpp"
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/string_cast.hpp>
#include <iostream>
#include <webgpu/raii/CombinedComputePipeline.h>
#include <webgpu/raii/base_types.h>
#include <webgpu_engine/Buffer.h>

using namespace webgpu_engine;

TEST_CASE("encoder functions")
{
    UnittestWebgpuContext context;


    SECTION("octahedron normal encoding")
    {
        const int random_normals_count = 200;

        const char* wgsl_single_thread_octahedron_test = R"(
            #include "encoder.wgsl"

            @group(0) @binding(0) var<storage, read_write> input_buffer: array<vec4f>;
            @group(0) @binding(1) var<storage, read_write> output_buffer: array<u32>;

            @compute @workgroup_size(1)
            fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
                let input_size = u32(input_buffer[0].w);

                // Go through all normals and encode/decode them and see if the result is similar
                // Write to the output_buffer a 1 if the encoding/decoding was not successfull, otherwise 0
                for (var i: u32 = 1u; i < input_size; i++) {
                    let normal = input_buffer[i].xyz;
                    let encoded = octNormalEncode2u16(normal);
                    let decoded = octNormalDecode2u16(encoded);

                    // Check if the decoded normal is approximately equal to the original normal
                    if (length(normal - decoded) > 0.001) { // Threshold for floating point comparison
                        output_buffer[i] = 1u;
                    }
                }
            }
        )";

        // ==== GENERATE RANDOM TEST SET WITH ADDITIONAL EDGE CASES ====
        std::vector<glm::vec4> test_normals_buffer_data;
        {
            std::vector<glm::vec3> testNormals = {
                glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), glm::vec3(1, 1, 0), glm::vec3(1, 0, 1), glm::vec3(0, 1, 1), glm::vec3(1, 1, 1),
                glm::vec3(-1, 0, 0),glm::vec3(0, -1, 0),glm::vec3(0, 0, -1),glm::vec3(-1, -1, 0),glm::vec3(-1, 0, -1),glm::vec3(0, -1, -1),glm::vec3(-1, -1, -1)
            };

            for (size_t i = 0; i < random_normals_count; i++) {
                testNormals.push_back(glm::ballRand(1.0f)); // Generates a random point inside a unit sphere
            }

            // Normalize and write to buffer data array. Vec4 is used as vec3 causes alignment issues! Again: NEVER USE VEC3
            for (size_t i = 0; i < testNormals.size(); i++) {
                test_normals_buffer_data.push_back(glm::vec4(glm::normalize(testNormals[i]), test_normals_buffer_data.size()));
            }
        }

        const webgpu::raii::CommandEncoder encoder(context.device, {});

        const auto output_buffer = std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(
            context.device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, test_normals_buffer_data.size(), "output buffer");
        const auto input_buffer = std::make_unique<webgpu::raii::RawBuffer<glm::vec4>>(
            context.device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, test_normals_buffer_data.size(), "input buffer");

        // Upload the test data to the input buffer
        input_buffer->write(context.queue, test_normals_buffer_data.data(), test_normals_buffer_data.size(), 0);

        // ==== CREATE BINDING LAYOUT AND BIND GROUP ====
        WGPUBindGroupLayoutEntry compute_input_binding {};
        compute_input_binding.binding = 0;
        compute_input_binding.visibility = WGPUShaderStage_Compute;
        compute_input_binding.buffer.type = WGPUBufferBindingType_Storage;
        compute_input_binding.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutEntry compute_output_binding {};
        compute_output_binding.binding = 1;
        compute_output_binding.visibility = WGPUShaderStage_Compute;
        compute_output_binding.buffer.type = WGPUBufferBindingType_Storage;
        compute_output_binding.buffer.minBindingSize = 0;

        auto compute_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(
            context.device, std::vector<WGPUBindGroupLayoutEntry> { compute_input_binding, compute_output_binding }, "octahedron test bind group layout");

        std::vector<WGPUBindGroupEntry> bindgroup_entries = {
            WGPUBindGroupEntry {
                .nextInChain =  nullptr, .binding = 0, .buffer = input_buffer->handle(), .offset = 0, .size = input_buffer->size_in_byte(), .sampler = nullptr, .textureView = nullptr
            },
            WGPUBindGroupEntry {
                .nextInChain =  nullptr, .binding = 1, .buffer = output_buffer->handle(), .offset = 0, .size = output_buffer->size_in_byte(), .sampler = nullptr, .textureView = nullptr
            }
        };

        auto compute_bind_group
            = std::make_unique<webgpu::raii::BindGroup>(context.device, *compute_bind_group_layout, bindgroup_entries, "octahedron test bindgroup");

        // ==== CREATE SHADER MODULE AND PIPELINE ====
        std::unique_ptr<webgpu::raii::ShaderModule> compute_shader_module
            = context.shader_module_manager->create_shader_module_for_file(wgsl_single_thread_octahedron_test);

        auto compute_pipeline = std::make_unique<webgpu::raii::CombinedComputePipeline>(
            context.device, *compute_shader_module, std::vector<const webgpu::raii::BindGroupLayout*> { compute_bind_group_layout.get() });

        // ==== RUN THE COMPUTE PIPELINE ====
        // NOTE: Needs to be in a separate scope to ensure the encoder is finished before submitting
        {
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), {});
            const glm::uvec3& workgroup_counts = { 1, 1, 1 };
            compute_pipeline->set_binding(0, *compute_bind_group.get());
            compute_pipeline->run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "octahedron test command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(context.queue, 1, &command);
        wgpuCommandBufferRelease(command);

        // ==== WAIT FOR THE WORK TO BE DONE AND FOR BUFFERS TO BE MAPPED ====
        bool done = false;
        wgpuQueueOnSubmittedWorkDone(
            context.queue,
            []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
                if (status != WGPUQueueWorkDoneStatus_Success) {
                    std::cerr << "Work done failed" << std::endl;
                }
                *reinterpret_cast<bool*>(user_data) = true;
            },
            &done);
        webgpu::waitForFlag(context.device, &done);

        std::vector<uint32_t> output;
        output_buffer->read_back_sync(context.device, output);
        REQUIRE(output.size() == test_normals_buffer_data.size());

        int failed_normals = 0;
        for (size_t i = 0; i < output.size(); i++) {
            if (output[i] != 0) {
                failed_normals++;
                std::cout << "Octahedron encoding failed with vector at index " << i << ": " << glm::to_string(glm::vec3(test_normals_buffer_data[i])) << std::endl;
            }
        }
        CHECK(failed_normals == 0); // None of the normals should have failed the encoding/decoding process
    }

}
