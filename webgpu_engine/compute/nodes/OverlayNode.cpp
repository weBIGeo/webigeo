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

#include "OverlayNode.h"

#include <QDebug>
#include <webgpu/webgpu.h>

namespace webgpu_engine::compute::nodes {

OverlayNode::OverlayNode()
    : OverlayNode(OverlaySettings {})
{
}

OverlayNode::OverlayNode(const OverlaySettings& settings)
    : Node({ InputSocket(*this, "texture", data_type<const webgpu::raii::TextureWithSampler*>()),
               InputSocket(*this, "region aabb", data_type<const radix::geometry::Aabb<2, double>*>()) },
          {})
    , m_settings(settings)
{
}

void OverlayNode::run_impl()
{
    if (m_update_func && input_socket("texture").is_socket_connected() && input_socket("region aabb").is_socket_connected()) {
        const auto* texture = std::get<data_type<const webgpu::raii::TextureWithSampler*>()>(input_socket("texture").get_connected_data());
        const auto* aabb = std::get<data_type<const radix::geometry::Aabb<2, double>*>()>(input_socket("region aabb").get_connected_data());

        bool copy = m_settings.copy;
        if (copy && texture && !(texture->texture().descriptor().usage & WGPUTextureUsage_CopySrc)) {
            qWarning() << "OverlayNode: source texture lacks CopySrc usage; falling back to linking instead of copying.";
            copy = false;
        }

        m_update_func(texture, *aabb, copy);
    }
    complete_run();
}

} // namespace webgpu_engine::compute::nodes
