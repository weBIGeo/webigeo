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

#include "TileSourcePresets.h"

#include <QDebug>

namespace nucleus::tile {

namespace {
    
const std::vector<TileSourcePreset> presets = {
    { "Gataki Ortho", "ortho", "https://gataki.cg.tuwien.ac.at/raw/basemap/tiles/", TileLoadService::UrlPattern::ZYX_yPointingSouth, ".jpeg", 256,
        19 },
    { "Basemap Ortho", "basemap_ortho", "https://mapsneu.wien.gv.at/basemap/bmaporthofoto30cm/normal/google3857/",
        TileLoadService::UrlPattern::ZYX_yPointingSouth, ".jpeg", 256, 20 },
    { "Basemap Gelände", "basemap_gelaende", "https://mapsneu.wien.gv.at/basemap/bmapgelaende/grau/google3857/",
        TileLoadService::UrlPattern::ZYX_yPointingSouth, ".jpeg", 256, 17 },
    { "Basemap Oberfläche", "basemap_oberflaeche", "https://mapsneu.wien.gv.at/basemap/bmapoberflaeche/grau/google3857/",
        TileLoadService::UrlPattern::ZYX_yPointingSouth, ".jpeg", 256, 17 },
    { "Geoland Basemap", "basemap_color", "https://mapsneu.wien.gv.at/basemap/geolandbasemap/normal/google3857/",
        TileLoadService::UrlPattern::ZYX_yPointingSouth, ".png", 256, 20 },
    { "Geoland Basemap Grau", "basemap_grau", "https://mapsneu.wien.gv.at/basemap/bmapgrau/normal/google3857/",
        TileLoadService::UrlPattern::ZYX_yPointingSouth, ".png", 256, 20 },
    { "Basemap High DPI", "basemap_hidpi", "https://mapsneu.wien.gv.at/basemap/bmaphidpi/normal/google3857/",
        TileLoadService::UrlPattern::ZYX_yPointingSouth, ".jpeg", 512, 20 },
    { "Geoland Basemap Overlay", "basemap_overlay", "https://mapsneu.wien.gv.at/basemap/bmapoverlay/normal/google3857/",
        TileLoadService::UrlPattern::ZYX_yPointingSouth, ".png", 256, 20 },
};
} // namespace

namespace tile_source_presets {

const std::vector<TileSourcePreset>& all() { return presets; }

const TileSourcePreset* get(const QString& source_name)
{
    for (const auto& preset : presets)
        if (preset.source_name == source_name)
            return &preset;
    qWarning() << "Tile source preset" << source_name << "not existing.";
    return nullptr;
}

} // namespace tile_source_presets
} // namespace nucleus::tile
