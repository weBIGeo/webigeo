/*****************************************************************************
 * weBIGeo
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

#include "url_tools.h"

#ifdef _WIN32
#include <windows.h> // For ShellExecuteA on Windows
#elif __APPLE__
#include <cstdlib> // For system() on macOS
#elif __linux__
#include <cstdlib> // For system() on Linux
#elif __EMSCRIPTEN__
#include <emscripten.h> // For emscripten_run_script on Emscripten
#endif

namespace webgpu_app::util {

void open_website(const std::string& url)
{
#ifdef _WIN32
    ShellExecuteA(0, 0, url.c_str(), 0, 0, SW_SHOW);
#elif __APPLE__
    std::string command = "open " + url;
    system(command.c_str());
#elif __linux__
    std::string command = "xdg-open " + url;
    system(command.c_str());
#elif __EMSCRIPTEN__
    std::string script = "window.open('" + url + "', '_blank');";
    emscripten_run_script(script.c_str());
#endif
}

} // namespace webgpu_app::util
