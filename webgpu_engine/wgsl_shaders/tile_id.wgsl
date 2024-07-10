/*****************************************************************************
 * Alpine Terrain Renderer
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

fn hash_tile_id_vec(id: vec3<u32>) -> u32 {
    let z = id.z * 46965u + 10859u;
    let x = id.x * 60197u + 12253u;
    let y = id.y * 62117u + 59119u;
    return (x + y + z) & 65535u;
}

fn hash_tile_id(id: TileId) -> u32 {
    return hash_tile_id_vec(vec3<u32>(id.x, id.y, id.zoomlevel));
}

// currently it is not possible to pass pointers to runtime-sized arrays to functions
// see https://github.com/gpuweb/gpuweb/issues/2268#issuecomment-1788657300
//fn hashmap_get_position_in_value_buffer(key: vec3<u32>, key_buffer: ptr<storage, array<vec4<u32>>>) -> u32 {
//    var hash = hash_tile_id(key);
//    while(any((*key_buffer)[hash].xyz != key) && (*key_buffer)[hash].z != EMPTY_TILE_ZOOMLEVEL) {
//        hash++;
//    }
//    return hash;
//}
