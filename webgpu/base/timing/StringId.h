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

#include <cstdint>

namespace webgpu::timing {

namespace detail {
constexpr uint32_t fnv1a(const char* str, uint32_t hash = 2166136261u)
{
    return *str == '\0' ? hash : fnv1a(str + 1, (hash ^ static_cast<uint32_t>(*str)) * 16777619u);
}
} // namespace detail

// Lightweight timer identity: hash is computed constexpr from name at the callsite.
struct StringId {
    uint32_t hash;
    const char* name;
    const char* group;

    constexpr StringId(const char* name, const char* group)
        : hash(detail::fnv1a(name))
        , name(name)
        , group(group)
    {
    }
};

} // namespace webgpu::timing
