# WebGPU App structure

This document gives a brief overview of `apps/webgpu_app`, the main weBIGeo application, and how it relates to the underlying libraries (`nucleus`, `webgpu_engine`, `webgpu_compute`).

## Library dependencies

```mermaid
graph TD
    webgpu_app --> nucleus
    webgpu_app --> webgpu_engine
    webgpu_app --> webgpu
    webgpu_app -->|optional| webgpu_compute
    webgpu_engine --> nucleus
    webgpu_engine --> webgpu
    webgpu_compute --> nucleus
    webgpu_compute --> webgpu

    linkStyle 3 stroke:#e8590c,stroke-width:2px,stroke-dasharray: 6 4
    classDef optional fill:#fff3e0,stroke:#e8590c,stroke-width:2px,color:#e8590c
    class webgpu_compute optional
```

- `webgpu` is the base library wrapping the WebGPU API (device, pipelines, buffers, ...) and is used by everything else.
- `nucleus` provides the shared base infrastructure (tile management, camera, data structures, ...) and is used by everything else.
- `webgpu_engine` implements the terrain rendering pipeline (tiles, shading, overlays) on top of `nucleus`/`webgpu`.
- `webgpu_compute` implements GPU compute nodes (e.g. avalanche simulation, snow, trajectory computation) on top of `nucleus`/`webgpu`.
- `webgpu_engine` and `webgpu_compute` do **not** depend on each other.
- The dashed edge marks the only optional link: `webgpu_app` links `webgpu_compute` (and the node-graph UI that goes with it) only if the CMake option `ALP_WEBGPU_APP_ENABLE_COMPUTE` is set (see `apps/webgpu_app/CMakeLists.txt`). When off, the app only renders terrain and overlays, without any compute/simulation features.

## Application structure

`apps/webgpu_app/main.cpp` creates the `webgpu_app::App`, which owns the SDL window, the WebGPU device, and the per-frame `render()` loop (`App.cpp`). Each frame:

1. The 3D scene (terrain, overlays) is rendered via `webgpu_engine`.
2. `ImGuiManager::render()` records the ImGui draw commands for the UI on top of the scene.

### ImGuiManager

`ImGuiManager` (`apps/webgpu_app/ImGuiManager.h/.cpp`) is the central piece tying the UI together:
- Initializes the Dear ImGui / ImNodes contexts and fonts.
- Owns the list of `ImGuiPanel`s and draws them every frame.
- Forwards SDL events to ImGui and exposes whether ImGui wants to capture input.
- Can toggle the whole UI on/off.

### Panels (`apps/webgpu_app/ui`)

Panels are UI windows/sidebar sections, all implementing the `ImGuiPanel` interface (`draw_panel()` for sidebar content, `draw()` for floating windows). Examples: `AppPanel` (general settings), `CameraPanel`, `ShadingPanel`, `TimingPanel`, `SearchPanel`, `CompassPanel`, `LogoPanel`, `AboutPanel`.

If `ALP_WEBGPU_APP_ENABLE_COMPUTE` is enabled, an additional `NodeGraphPanel` (`apps/webgpu_app/compute`) is registered, providing a visual node-graph editor for the `webgpu_compute` nodes (release points, avalanche trajectories, snow, tile I/O, ...).

### Overlay (`apps/webgpu_app/overlay`)

"Overlay" here refers to data visualized on top of the terrain in the 3D viewport (height lines, screen-space snow, textures, tile debug info) — not a UI overlay. `OverlaysPanel` is the ImGui panel used to configure which overlays are active and their settings; the actual rendering is done by the corresponding `webgpu_engine::OverlayRenderer`s. Each overlay has a small `OverlayImGuiRenderer` subclass that draws its settings controls.

### Compute (`apps/webgpu_app/compute`)

Only compiled when `ALP_WEBGPU_APP_ENABLE_COMPUTE` is enabled. Contains the `NodeGraphPanel` and a `NodeRenderer` per `webgpu_compute` node type, providing the visual/interactive representation of the compute graph (e.g. `AvalancheTrajectoriesNode`, `ReleasePointsNode`, `SnowNode`, `RequestTilesNode`, `ExportNode`). These nodes can route their output to overlays via `OverlayRenderNode`.
