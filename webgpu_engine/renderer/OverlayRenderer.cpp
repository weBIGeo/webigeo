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

#include "OverlayRenderer.h"

namespace webgpu_engine {

OverlayRenderer::OverlayRenderer()
    : QObject { nullptr }
{
}

void OverlayRenderer::init(WGPUDevice device) { m_device = device; }

void OverlayRenderer::resize(int /*w*/, int /*h*/) { /* TODO: implement */ }

void OverlayRenderer::draw(const WGPUCommandEncoder& /*command_encoder*/) { /* TODO: implement */ }

void OverlayRenderer::set_pipeline_manager(const PipelineManager& pipeline_manager) { m_pipeline_manager = &pipeline_manager; }

} // namespace webgpu_engine
