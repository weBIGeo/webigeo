# weBIGeo Build Setup Notes

## System Information

- **Machine**: Intel x64
- **GPUs**: GPU with Vulkan support
- **OS**: Windows

## Prerequisites

- Visual Studio 2022 with C++ workload (~15-30 min install)
- CMake 3.25+ (~1 min via winget)
- Ninja (~1 min via winget)
- Qt 6.10.1+ (MSVC2022 build) (~10-20 min via Qt Maintenance Tool)
- Vulkan SDK (for Dawn Vulkan backend)

```batch
# 1. Install Emscripten SDK (sibling directory ../emsdk)
scripts\install_emsdk.bat

# 2. Install emdawnwebgpu package (sibling directory ../emdawnwebgpu)
scripts\install_emdawnwebgpu.bat
```

### External Library Compilation

All build scripts are located in the `scripts/` folder:

```batch
# 1. Build Dawn (sibling directory ../dawn)
scripts\build_dawn.bat          # ~30-45 min

# 2. Build SDL2 (sibling directory ../SDL)
scripts\build_sdl.bat           # ~5 min

# 3. Build weBIGeo
scripts\build_webigeo.bat       # ~5-10 min (first build)
```

## Build Times Summary

| Step                                     | Duration       |
| ---------------------------------------- | -------------- |
| CMake install                            | ~1 min         |
| Ninja install                            | ~1 min         |
| Visual Studio 2022 install               | ~15-30 min     |
| Qt 6.10.1 install                        | ~10-20 min     |
| Dawn clone + build (Debug + Release)     | ~30-45 min     |
| SDL2 clone + build                       | ~5 min         |
| weBIGeo build (470 targets, first build) | ~5-10 min      |
| **Total (from scratch)**                 | **~1-2 hours** |

---
