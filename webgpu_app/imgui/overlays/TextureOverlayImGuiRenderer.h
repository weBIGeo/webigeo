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

#include "OverlayImGuiRenderer.h"
#include <QObject>
#include <string>
#include <webgpu_engine/renderer/overlays/TextureOverlay.h>

namespace webgpu_app {

class TextureOverlayImGuiRenderer : public QObject, public OverlayImGuiRenderer {
    Q_OBJECT
public:
    explicit TextureOverlayImGuiRenderer(webgpu_engine::TextureOverlay& overlay);

    std::string display_name() const override { return "Texture Overlay"; }
    bool render_custom_settings() override;

private slots:
    void on_file_uploaded(const std::string& filename, const std::string& tag);

private:
    void apply_image_file(const std::string& path);
    void apply_aabb_from_file(const std::string& path);

    webgpu_engine::TextureOverlay* m_texture_overlay;
    std::string m_last_dialog_directory;
    std::string m_loaded_image_path;
    double m_aabb[4] = { 0.0, 0.0, 1.0, 1.0 };
    bool m_needs_redraw = false;
    std::string m_png_tag;
    std::string m_aabb_tag;
};

} // namespace webgpu_app
