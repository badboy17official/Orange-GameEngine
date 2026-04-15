# Engine Status

Last updated: 2026-04-15

## Current scope
- Target OS: Linux
- Current distro target: Arch Linux
- Primary graphics backend: Vulkan
- Current runtime default backend: Vulkan
- OpenGL backend: not implemented
- DirectX backend: not implemented
- Future Windows backend direction: DirectX 11 only, not DirectX 12

## Current working state
- Project builds successfully with CMake on Linux.
- Runtime reaches the Vulkan backend successfully on the current Arch Linux + NVIDIA setup.
- Terminal gameplay loop is playable with `WASD`, `SPACE`, and `Q`.
- Fixed-step simulation loop is active at 60 Hz.
- Renderer runs the current optimization-focused pass pipeline:
- `visibility`
- `depth_prepass`
- `shadow`
- `lighting`
- `transparent`
- `post`
- ASCII frame output works in terminal.
- Diagnostics overlay reports CPU timings, GPU timings, visible counts, shadow budget usage, frame budget status, and active RHI backend.
- RHI abstraction is in place with `null` and `vulkan`.
- Vulkan GPU timestamp instrumentation is working in the current environment.

## Confirmed environment status
- Vulkan is installed and available on the current system.
- Current detected Vulkan GPU path is NVIDIA proprietary driver on GeForce RTX 2050.
- Hybrid GPU system is present:
- NVIDIA GeForce RTX 2050
- AMD Radeon Vega iGPU

## What is already implemented
- Core engine loop
- Input manager
- Basic game state and enemy/player simulation
- Terminal renderer
- Optimization-oriented pass structure
- Frame diagnostics and profiler output
- Vulkan-first backend selection
- Vulkan init failure logging and `null` fallback path
- Build configuration for Linux with Vulkan required by default

## What is not finished yet
- Real Vulkan presentation path with swapchain and actual windowed output
- Full raster renderer instead of terminal-only ASCII output
- Render graph with resource lifetime tracking and barrier validation
- Validation layer toggle for debug builds
- Non-blocking GPU timestamp resolve path
- Accurate GPU bandwidth and counter reporting
- Asset import pipeline
- Texture compression pipeline
- Material and shader pipeline
- Mesh optimization and LOD pipeline
- Runtime asset streaming and budget control
- Particle, decal, water, and volumetric rendering expansions
- Job system / task graph
- ECS / scene data model
- Memory tracking and allocator strategy
- CI perf gates and regression coverage

## Highest-priority next tasks
1. Add Vulkan validation-layer enable switch for debug builds.
2. Build the render graph skeleton before adding more rendering features.
3. Replace ASCII-only output path with real Vulkan presentation path.
4. Improve GPU timestamp resolve so it does not block the frame unnecessarily.
5. Start the asset pipeline with textures, compression, and material/shader build flow.

## Practical current limitations
- Engine output is still terminal-based, not a real windowed renderer.
- Vulkan usage is currently focused on instrumentation and backend groundwork, not full scene presentation.
- GPU timing data is lightweight and not yet a complete GPU profiling system.
- Several metrics are still estimated rather than sourced from real GPU counters.

## Resume guidance
- Start from Vulkan debug validation and backend robustness.
- Then move to render graph skeleton.
- Then implement swapchain + presentation.
- Only after that expand rendering features and asset pipeline.

