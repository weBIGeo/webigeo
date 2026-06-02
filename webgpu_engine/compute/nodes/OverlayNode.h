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

#pragma once

#include "Node.h"
#include <functional>
#include <radix/geometry.h>
#include <webgpu/raii/TextureWithSampler.h>

namespace webgpu_engine::compute::nodes {

// Terminal node that hands the graph's result texture + world-space aabb to a
// consumer (typically a TextureOverlay in the render app) via an injected callback.
//
// The node deliberately knows nothing about the rendering layer: the callback only
// uses compute-layer types, so the compute target carries no dependency on the
// renderer. An unset callback makes run a no-op (e.g. a headless compute app).
class OverlayNode : public Node {
    Q_OBJECT

public:
    NODE_TYPE_NAME(OverlayNode)

    struct OverlaySettings {
        // false: link the source texture directly (non-owning).
        // true: copy the source into the consumer's own texture (requires CopySrc on the source).
        bool copy = false;
    };

    // copy == true asks the consumer to copy the texture; false asks it to link it.
    using UpdateFunc = std::function<void(const webgpu::raii::TextureWithSampler* texture, const radix::geometry::Aabb<2, double>& aabb, bool copy)>;

    OverlayNode();
    explicit OverlayNode(const OverlaySettings& settings);

    void set_update_func(UpdateFunc func) { m_update_func = std::move(func); }

    void set_settings(const OverlaySettings& settings) { m_settings = settings; }
    const OverlaySettings& get_settings() const { return m_settings; }

public slots:
    void run_impl() override;

private:
    UpdateFunc m_update_func;
    OverlaySettings m_settings;
};

} // namespace webgpu_engine::compute::nodes
