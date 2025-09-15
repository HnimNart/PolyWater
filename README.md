# VulkanRenderer

A modular rendering framework designed to support **multiple platforms** and **multiple graphics backends** (e.g., Vulkan, Direct3D12, Metal, WebGPU).
It can be used for both **rasterization** and **ray tracing**, with a strong focus on modularity, extensibility, and maintainability.

---

## Project Structure

### Applications
app/
├─ sandbox/       # Example app using the engine
├─ editor/        # Optional editor/front-end
└─ tests/         # Rendering and regression tests

### Engine
engine/
├─ core/          # Core utilities (no platform/API deps)
│   ├─ math/          # linear algebra, geometry helpers
│   ├─ containers/    # span, small_vector, hash maps
│   ├─ memory/        # allocators, arenas
│   ├─ logging/       # logging, assertions
│   └─ threading/     # jobs, fibers, task graph basics
│
├─ os/            # Platform abstraction layer (PAL)
│   ├─ win32/         # Win32 + XInput + WGI
│   ├─ linux/         # X11/Wayland + evdev
│   ├─ macos/         # Cocoa/MetalLayer, IOKit
│   └─ windowing/     # SDL/GLFW integration
│
├─ gfx/           # Optional API-agnostic rendering facade
│   └─ interfaces/    # IGpuDevice, handles, descriptors
│
├─ rhi/           # Render Hardware Interface (API-agnostic layer)
│   ├─ rhi_device.h     # IDevice (caps, queues)
│   ├─ rhi_resources.h  # BufferDesc, ImageDesc, SamplerDesc, Views
│   ├─ rhi_pipeline.h   # RasterPipelineDesc, RTPipelineDesc
│   ├─ rhi_cmd.h        # ICommandList, barriers by usage
│   ├─ rhi_swapchain.h
│   ├─ rhi_shaders.h    # Shader bytecode + reflection schema
│   ├─ rhi_enums.h      # Formats, usage, stages, access, queue types
│   └─ rhi_utils.h      # Helpers, debug names
│
├─ backends/      # Backend implementations
│   ├─ vulkan/         # Vulkan
│   │   vk_device.*, vk_buffer.*, vk_image.*, vk_pipeline.*, vk_rt.*
│   ├─ d3d12/          # Direct3D12
│   │   dx_device.*, dx_heap.*, dx_descriptor.*, dx_pipeline.*, dx_rt.*
│   ├─ metal/          # Metal
│   │   mtl_device.*, mtl_buffer.*, mtl_texture.*, mtl_pipeline.*, mtl_rt.*
│   └─ webgpu/         # WebGPU (e.g. Dawn)
│       wgpu_device.*, ...
│
├─ shader/        # Shader system
│   ├─ compiler/      # DXC/glslang wrappers
│   ├─ reflection/    # SPIR-V reflection
│   ├─ packer/        # package SPIR-V + metadata
│   └─ codegen/       # generate C++ headers for layouts
│
├─ res/           # Runtime resource & asset system
│   ├─ vfs/           # virtual FS: .pak, loose files, http
│   ├─ asset_db/      # asset registry, dependencies
│   ├─ loaders/       # mesh_loader, texture_loader, material_loader
│   ├─ upload/        # staging buffers, async GPU upload
│   └─ cache/         # RAM/GPU residency management
│
├─ scene/         # Scene representation
│   ├─ components/    # Mesh, Material, Transform, Light, Camera
│   ├─ systems/       # Mesh system, material system, RT AS builder
│   └─ ecs/           # optional: entity/component framework
│
├─ graph/         # Framegraph
│   ├─ core/          # logical resources, passes
│   ├─ compile/       # dependency analysis, scheduling
│   └─ execute/       # barrier emission, cmd buffer recording
│
├─ task/          # Parallelism
│   ├─ scheduler/     # job system
│   └─ fiber/         # optional fiber-based tasks
│
└─ modules/       # Feature modules (plug into framegraph)
    ├─ taa/           # temporal AA
    ├─ tonemap/
    ├─ imgui/
    ├─ sky/
    ├─ pathtracer/
    └─ shadows/

### Offline Tools
tools/
├─ assetc/        # Asset compiler
│   ├─ importers/     # glTF, OBJ, FBX, USD
│   ├─ processors/    # meshopt, meshlets, LODs, tangent calc
│   ├─ materializer/  # pack PBR params, transcode textures
│   └─ packer/        # bundle into .pak/.bundle
│
├─ shadersync/    # Offline shader build & reflection
└─ validation/    # Compare GPU results with reference

### Third Party Deps
third_party/
├─ vma/           # Vulkan Memory Allocator
├─ spirv-tools/
├─ spirv-reflect/
├─ meshoptimizer/
├─ basisu/
└─ stb/

### Documentation
docs/             # Design docs, diagrams, samples
