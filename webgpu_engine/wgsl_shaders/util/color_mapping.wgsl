/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2023 Gerald Kimmersdorfer
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

// value in [0,1], represents angle from horizontal to vertical
// color map from https://www.bergfex.com/
// values not in [0,1] are mapped to black
fn color_mapping_bergfex(value: f32) -> vec3f {
    if (value < 0.0 || value > 1.0) {
        return vec3f(0);
    }

    let deg = u32(value * 90);

    const bins = array<u32, 5>(29, 34, 39, 44, 90);
    const colors = array<vec3f, 5>(
        vec3f(1, 1, 1),
        vec3f(255.0/255.0, 219.0/255.0,  17.0/255.0),
        vec3f(255.0/255.0, 117.0/255.0,   6.0/255.0),
        vec3f(213.0/255.0,   0.0/255.0,   0.0/255.0),
        vec3f(100.0/255.0,   0.0/255.0, 213.0/255.0),
    );
    
    var index = 0;
    while (deg > bins[index]) {
        index++;
    }
    return colors[index];
}

// value in [0,1], represents angle from horizontal to vertical
// color map from https://www.openslopemap.org/karte/
// values not in [0,1] are mapped to black
fn color_mapping_openslopemap(value: f32) -> vec3f {
    if (value < 0.0 || value > 1.0) {
        return vec3f(0);
    }

    let deg = u32(value * 90);

    const bins = array<u32, 9>(9, 29, 34, 39, 42, 45, 49, 54, 90);
    const colors = array<vec3f, 9>(
        vec3f(254.0/255.0, 249.0/255.0, 249.0/255.0),
        vec3f( 51.0/255.0, 249.0/255.0,  49.0/255.0),
        vec3f(242.0/255.0, 228.0/255.0,  44.0/255.0),
        vec3f(255.0/255.0, 169.0/255.0,  45.0/255.0),
        vec3f(255.0/255.0,  48.0/255.0,  45.0/255.0),
        vec3f(255.0/255.0,  79.0/255.0, 249.0/255.0),
        vec3f(183.0/255.0,  69.0/255.0, 253.0/255.0),
        vec3f(135.0/255.0,  44.0/255.0, 253.0/255.0),
        vec3f( 49.0/255.0,  49.0/255.0, 253.0/255.0),
    );
    
    var index = 0;
    while (deg > bins[index]) {
        index++;
    }
    return colors[index];
}

// value in [0,1], just use red color channel
fn color_mapping_red(value: f32) -> vec3f {
    return vec3f(value, 0, 0);
}