#ifdef __APPLE__

#import "MetalDeviceAssets.hpp"

#import <Metal/Metal.h>

#import "backend/metal/core/MetalContextManager.hpp"
#import "scene/Scene.h"
#import "shaders/shared/structs.h"

#include <algorithm>
#include <iterator>

#include <iostream>
#include <cstdio>
#include <iomanip>

void dumpMeshPrimitiveDeep(const shaderio::MeshPrimitive& mesh) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&mesh);
    size_t totalSize = sizeof(shaderio::MeshPrimitive);

    std::printf("\n--- MeshPrimitive Deep Memory Dump (%zu bytes) ---\n", totalSize);
    std::printf("%-8s | %-32s | %s\n", "Offset", "Bytes (Hex)", "Structure / Values");
    std::printf("--------------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < totalSize; i += 16) {
        std::printf("0x%03zx    | ", i);

        // Print Hex Bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < totalSize) {
                std::printf("%02x ", raw[i + j]);
            } else {
                std::printf("   ");
            }
            if ((j + 1) % 4 == 0 && j < 15) std::printf(" ");
        }

        std::printf(" | ");

        // --- Interpretation Logic ---
        // Offset 0: Pointer (8 bytes)
        if (i == 0) {
            uint64_t ptr; std::memcpy(&ptr, &raw[i], 8);
            std::printf("Pointer: 0x%016llx", ptr);
        }
        // TriangleMesh starts after the pointer. 
        // Based on your struct, if TriangleMesh starts at Offset 8:
        else if (i >= 8 && i < 80) { 
            // Calculate which BufferView we are in (each is 12 bytes + potential 4 byte pad)
            size_t meshOffset = i - 8;
            const char* views[] = {"Indices", "Positions", "Normals", "Colors", "TexCoords", "Tangents"};
            int viewIdx = (int)(meshOffset / 16); // Assuming 16-byte alignment per view
            
            if (viewIdx < 6) {
                uint32_t vals[3];
                std::memcpy(vals, &raw[i], 12);
                std::printf("%-10s: {off:%u, cnt:%u, str:%u}", views[viewIdx], vals[0], vals[1], vals[2]);
            }
        }
        else if (i == 144) { // Typical offset for the trailing fields
             uint32_t rIdx; int iType;
             std::memcpy(&rIdx, &raw[i], 4);
             std::memcpy(&iType, &raw[i+4], 4);
             std::printf("rawBufIdx: %u, idxType: %d", rIdx, iType);
        }

        std::printf("\n");
    }
    std::printf("--------------------------------------------------------------------------------------\n\n");
}



void dumpInstanceMemory(const shaderio::Instance& inst) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&inst);
    size_t totalSize = sizeof(shaderio::Instance);

    std::printf("\n--- Instance Memory Dump (%zu bytes) ---\n", totalSize);
    std::printf("%-8s | %-32s | %s\n", "Offset", "Bytes (Hex)", "Interpreted Values");
    std::printf("--------------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < totalSize; i += 16) {
        // 1. Print Offset
        std::printf("0x%03zx    | ", i);

        // 2. Print Hex Bytes for this 16-byte block
        for (size_t j = 0; j < 16; ++j) {
            std::printf("%02x ", raw[i + j]);
            if ((j + 1) % 4 == 0 && j < 15) std::printf(" ");
        }

        std::printf(" | ");

        // 3. Print Interpreted Values based on your struct layout
        if (i == 0) { // Block 1: Translation (float3) + Pad
            float v[3]; std::memcpy(v, &raw[i], 12);
            std::printf("Trans: [%.2f, %.2f, %.2f]", v[0], v[1], v[2]);
        } 
        else if (i == 16) { // Block 2: Rotation (float4)
            float v[4]; std::memcpy(v, &raw[i], 16);
            std::printf("Rot:   [%.2f, %.2f, %.2f, %.2f]", v[0], v[1], v[2], v[3]);
        } 
        else if (i == 32) { // Block 3: Scale (float3) + MatIdx
            float v[3]; std::memcpy(v, &raw[i], 12);
            uint32_t mIdx; std::memcpy(&mIdx, &raw[i + 12], 4);
            std::printf("Scale: [%.2f, %.2f, %.2f] MatIdx: %u", v[0], v[1], v[2], mIdx);
        } 
        else if (i == 48) { // Block 4: Indices + Pads
            uint32_t idx[4]; std::memcpy(idx, &raw[i], 16);
            std::printf("Mesh: %u, Hit: %u, Pads: [%u, %u]", idx[0], idx[1], idx[2], idx[3]);
        } 
        else if (i >= 64) { // Blocks 5-8: Transform Matrix
            float row[4]; std::memcpy(row, &raw[i], 16);
            std::printf("Mat Row %zu: [%.2f, %.2f, %.2f, %.2f]", (i - 64) / 16, row[0], row[1], row[2], row[3]);
        }

        std::printf("\n");
    }
    std::printf("--------------------------------------------------------------------------------------\n\n");
}



// Per-mesh Metal GPU resources built from the raw mesh bytes.
struct MetalMeshBuffer {
  id<MTLBuffer> indexBuffer;
  uint32_t indexCount  = 0;
  bool     is32Bit     = false;
};

static shaderio::Instance toMetalInstance(shaderio::Instance instance) {
  // The Slang shaders are compiled with -matrix-layout-row-major for both
  // Vulkan and Metal targets.  With that flag, Slang interprets GPU buffer
  // bytes as row-major.  GLM stores matrices column-major, so Slang naturally
  // "sees" the mathematical transpose of every matrix it loads from a buffer.
  // That implicit transpose is exactly what mul(v, M) needs to compute the
  // correct column-vector transform (M * v).  No CPU-side transpose is needed
  // or correct here — adding one causes Slang to see the original matrix
  // instead of its transpose, which corrupts the w component and produces the
  // "stretched towards camera" artifact.
  return instance;
}

struct MetalDeviceAssetsData {
  id<MTLDevice> device;

  // Raw upload buffers (one per upload() call)
  std::unordered_map<IDeviceAssets::BufferID, id<MTLBuffer>> rawBuffers;

  // mesh ID → buffer ID (set by linkMeshToBuffer)
  std::unordered_map<IDeviceAssets::MeshID, IDeviceAssets::BufferID>
      meshToBuffer;

  // mesh ID → ready-to-draw Metal buffers (built in uploadSceneResoures)
  std::unordered_map<IDeviceAssets::MeshID, MetalMeshBuffer> meshBuffers;

  // ---------------------------------------------------------------------------
  // Slang-shader GPU scene resource buffers
  // ---------------------------------------------------------------------------
  // MeshPrimitive[] with each element's 'buffer' field set to the GPU virtual
  // address of the raw mesh data Metal buffer.
  id<MTLBuffer> meshPrimitivesGpuBuffer;
  // Instance[] array buffer.
  id<MTLBuffer> instancesGpuBuffer;
  // Material[] array buffer.
  id<MTLBuffer> materialsGpuBuffer;
  // Single shaderio::SceneResources struct whose pointer fields hold GPU addresses.
  id<MTLBuffer> sceneResourcesGpuBuffer;
  // Single shaderio::SceneInfo struct updated per frame via updateSceneInfo().
  id<MTLBuffer> sceneInfoGpuBuffer;

  IDeviceAssets::BufferID nextId = 0;
};

// ---------------------------------------------------------------------------
// MetalDeviceAssets implementation
// ---------------------------------------------------------------------------

/**********************************************************/
MetalDeviceAssets::MetalDeviceAssets(MetalContextManager *ctx) : m_ctx(ctx)
/**********************************************************/
{
  m_data         = std::make_unique<MetalDeviceAssetsData>();
  m_data->device = (__bridge id<MTLDevice>)m_ctx->getDeviceHandle();
}

/**********************************************************/
MetalDeviceAssets::~MetalDeviceAssets()
/**********************************************************/
{
  deinit();
}

/**********************************************************/
void MetalDeviceAssets::deinit()
/**********************************************************/
{
  m_data->rawBuffers.clear();
  m_data->meshBuffers.clear();
  m_data->meshToBuffer.clear();
  m_data->meshPrimitivesGpuBuffer  = nil;
  m_data->instancesGpuBuffer       = nil;
  m_data->materialsGpuBuffer       = nil;
  m_data->sceneResourcesGpuBuffer  = nil;
  m_data->sceneInfoGpuBuffer       = nil;
}

/**********************************************************/
IDeviceAssets::BufferHandle
MetalDeviceAssets::upload(const std::span<const uint8_t> &data)
/**********************************************************/
{
  if (data.empty()) {
    return {};
  }

  id<MTLBuffer> buf =
      [m_data->device newBufferWithBytes:data.data()
                                  length:data.size()
                                 options:MTLResourceStorageModeShared];
  printf("%d %d %p\n", data.size(), data.size_bytes(), buf.gpuAddress);

  IDeviceAssets::BufferID id = m_data->nextId++;
  m_data->rawBuffers.insert_or_assign(id, buf);

  return BufferHandle{.address = (uint8_t*)buf.gpuAddress, .id = id};
}

/**********************************************************/
void MetalDeviceAssets::destroyBuffer(BufferID id)
/**********************************************************/
{
  m_data->rawBuffers.erase(id);
  m_data->meshBuffers.erase(id); // also clear any derived buffer
}

/**********************************************************/
void MetalDeviceAssets::linkMeshToBuffer(MeshID meshId, BufferID bufferId)
/**********************************************************/
{
  printf("%d %d\n", meshId, bufferId);
  m_data->meshToBuffer[meshId] = bufferId;
}

/**********************************************************/
void MetalDeviceAssets::uploadSceneResoures(const Scene &resources)
/**********************************************************/
{
  // For every mesh, build a compact index buffer from the raw data already
  // uploaded to rawBuffers.  Vertex positions, normals, and UVs are read
  // directly from the raw buffer by the shader using the BufferView offsets
  // and strides stored in MeshPrimitive.triMesh — the same layout used by
  // the Vulkan renderer, so both renderers share identical geometry layout.
  for (uint32_t meshIdx = 0; meshIdx < resources.meshes.size(); ++meshIdx) {
    const shaderio::MeshPrimitive &prim = resources.meshes[meshIdx];
    if (prim.rawBufferIndex >= resources.meshData.size()) {
      continue;
    }
    const std::vector<uint8_t> &rawData = resources.meshData[prim.rawBufferIndex];
    if (rawData.empty()) {
      continue;
    }

    const shaderio::TriangleMesh &tri = prim.triMesh;
    const uint32_t indexCount  = tri.indices.count;
    if (indexCount == 0) {
      continue;
    }

    // ----------------------------------------------------------------
    // Build index buffer
    // ----------------------------------------------------------------
    const bool is32Bit = (prim.indexType == IndexType32);
    const uint32_t indexElemSize = is32Bit ? 4u : 2u;
    const uint32_t idxStride =
        tri.indices.byteStride == 0 ? indexElemSize : tri.indices.byteStride;

    // Compact indices into a tightly-packed buffer
    std::vector<uint8_t> indexData(indexCount * indexElemSize);
    for (uint32_t i = 0; i < indexCount; ++i) {
      const uint8_t *src = rawData.data() + tri.indices.offset + i * idxStride;
      std::memcpy(indexData.data() + i * indexElemSize, src, indexElemSize);
    }

    id<MTLBuffer> ib = [m_data->device
        newBufferWithBytes:indexData.data()
                   length:indexData.size()
                  options:MTLResourceStorageModeShared];

    MetalMeshBuffer mb;
    mb.indexBuffer  = ib;
    mb.indexCount   = indexCount;
    mb.is32Bit      = is32Bit;

    m_data->meshBuffers[meshIdx] = mb;
  }

  // -----------------------------------------------------------------------
  // Build GPU-side scene resource buffers for the Slang-compiled shaders.
  //
  // The gltf_raster.slang vertex shader fetches vertex data entirely via
  // GPU-virtual-address pointers stored inside SceneResources / MeshPrimitive.
  // We populate those pointer fields here using MTLBuffer.gpuAddress (Metal 3+
  // / Apple Silicon) so the GPU can dereference them directly.
  // -----------------------------------------------------------------------
  id<MTLDevice> device = m_data->device;

  // 1. Build MeshPrimitive[] with each element's 'buffer' field set to the GPU
  //    address of the corresponding raw-data Metal buffer.
  for (const auto &mesh : resources.meshes) {
    dumpMeshPrimitiveDeep(mesh);
  }
  if (!resources.meshes.empty()) {
    m_data->meshPrimitivesGpuBuffer =
        [device newBufferWithBytes:resources.meshes.data()
                            length:resources.meshes.size() *
                                   sizeof(shaderio::MeshPrimitive)
                           options:MTLResourceStorageModeShared];
  }

  for (const auto &inst : resources.instances) {
    dumpInstanceMemory(inst);
  }
  // 2. Upload Instance[] array.
  if (!resources.instances.empty()) {
    std::vector<shaderio::Instance> metalInstances;
    metalInstances.reserve(resources.instances.size());
    std::transform(resources.instances.begin(), resources.instances.end(),
                   std::back_inserter(metalInstances), toMetalInstance);

    m_data->instancesGpuBuffer = [device
        newBufferWithBytes:metalInstances.data()
                    length:metalInstances.size() * sizeof(shaderio::Instance)
                   options:MTLResourceStorageModeShared];
  }

  // 3. Upload Material[] array.
  if (!resources.materials.empty()) {
    m_data->materialsGpuBuffer =
        [device newBufferWithBytes:resources.materials.data()
                            length:resources.materials.size() *
                                   sizeof(shaderio::Material)
                           options:MTLResourceStorageModeShared];
  }

  // 4. Build the SceneResources struct whose pointer fields hold GPU addresses
  //    of the arrays uploaded above.
  shaderio::SceneResources sr{};
  if (m_data->instancesGpuBuffer) {
    const uint64_t addr = [m_data->instancesGpuBuffer gpuAddress];
    std::memcpy(&sr.instances, &addr, sizeof(addr));
  }
  if (m_data->meshPrimitivesGpuBuffer) {
    const uint64_t addr = [m_data->meshPrimitivesGpuBuffer gpuAddress];
    std::memcpy(&sr.meshes, &addr, sizeof(addr));
  }
  if (m_data->materialsGpuBuffer) {
    const uint64_t addr = [m_data->materialsGpuBuffer gpuAddress];
    std::memcpy(&sr.materials, &addr, sizeof(addr));
  }
  m_data->sceneResourcesGpuBuffer =
      [device newBufferWithBytes:&sr
                          length:sizeof(shaderio::SceneResources)
                         options:MTLResourceStorageModeShared];

  // 5. Allocate the per-frame SceneInfo buffer (content updated each frame).
  m_data->sceneInfoGpuBuffer =
      [device newBufferWithLength:sizeof(shaderio::SceneInfo)
                          options:MTLResourceStorageModeShared];
}

/**********************************************************/
void *MetalDeviceAssets::getRawMetalBuffer(BufferID id) const
/**********************************************************/
{
  auto it = m_data->rawBuffers.find(id);
  if (it == m_data->rawBuffers.end()) {
    return nullptr;
  }
  return (__bridge void *)it->second;
}

/**********************************************************/
void *MetalDeviceAssets::getIndexMetalBuffer(MeshID meshId) const
/**********************************************************/
{
  auto it = m_data->meshBuffers.find(meshId);
  if (it == m_data->meshBuffers.end()) {
    return nullptr;
  }
  return (__bridge void *)it->second.indexBuffer;
}

/**********************************************************/
uint32_t MetalDeviceAssets::getIndexCount(MeshID meshId) const
/**********************************************************/
{
  auto it = m_data->meshBuffers.find(meshId);
  if (it == m_data->meshBuffers.end()) {
    return 0;
  }
  return it->second.indexCount;
}

/**********************************************************/
bool MetalDeviceAssets::is32BitIndex(MeshID meshId) const
/**********************************************************/
{
  auto it = m_data->meshBuffers.find(meshId);
  if (it == m_data->meshBuffers.end()) {
    return false;
  }
  return it->second.is32Bit;
}

/**********************************************************/
uint64_t MetalDeviceAssets::getSceneResourcesGpuAddress() const
/**********************************************************/
{
  if (!m_data->sceneResourcesGpuBuffer) {
    return 0;
  }
  return [m_data->sceneResourcesGpuBuffer gpuAddress];
}

/**********************************************************/
uint64_t MetalDeviceAssets::getSceneInfoGpuAddress() const
/**********************************************************/
{
  if (!m_data->sceneInfoGpuBuffer) {
    return 0;
  }
  return [m_data->sceneInfoGpuBuffer gpuAddress];
}

/**********************************************************/
void *MetalDeviceAssets::getSceneInfoMetalBuffer() const
/**********************************************************/
{
  return (__bridge void *)m_data->sceneInfoGpuBuffer;
}

/**********************************************************/
void *MetalDeviceAssets::getInstancesMetalBuffer() const
/**********************************************************/
{
  return (__bridge void *)m_data->instancesGpuBuffer;
}

/**********************************************************/
void *MetalDeviceAssets::getMeshPrimitivesMetalBuffer() const
/**********************************************************/
{
  return (__bridge void *)m_data->meshPrimitivesGpuBuffer;
}

/**********************************************************/
void *MetalDeviceAssets::getMaterialsMetalBuffer() const
/**********************************************************/
{
  return (__bridge void *)m_data->materialsGpuBuffer;
}

/**********************************************************/
void MetalDeviceAssets::updateSceneInfo(const shaderio::SceneInfo &info)
/**********************************************************/
{
  if (!m_data->sceneInfoGpuBuffer) {
    return;
  }

  // The CPU-side matrices are built with Vulkan conventions:
  //   projMatrix[1][1] *= -1  (Y-flip so Vulkan clip-space Y points down).
  //
  // Metal NDC uses Y-up (same as OpenGL), so we must undo that flip before
  // uploading to the GPU.  Vulkan rendering is unaffected — it reads from
  // the CPU-side SceneInfo directly.
  shaderio::SceneInfo metalInfo = info;

  // Undo the Vulkan Y-flip by negating the same element once more.
  metalInfo.projMatrix[1][1] *= -1.0f;

  // Recompute the combined and inverse matrices from the corrected projection.
  metalInfo.viewProjMatrix = metalInfo.projMatrix * metalInfo.viewMatrix;
  metalInfo.projInvMatrix  = glm::inverse(metalInfo.projMatrix);

  std::memcpy([m_data->sceneInfoGpuBuffer contents], &metalInfo,
              sizeof(shaderio::SceneInfo));
}

/**********************************************************/
void MetalDeviceAssets::useResources(void *renderCommandEncoderHandle) const
/**********************************************************/
{
  id<MTLRenderCommandEncoder> enc =
      (__bridge id<MTLRenderCommandEncoder>)renderCommandEncoderHandle;

  // Helper: call useResource only when the buffer is non-nil.
  const MTLRenderStages vsfs = MTLRenderStageVertex | MTLRenderStageFragment;
  const MTLRenderStages vs   = MTLRenderStageVertex;

  auto use = [&](id<MTLBuffer> buf, MTLRenderStages stages) {
    if (buf) {
      [enc useResource:buf usage:MTLResourceUsageRead stages:stages];
    }
  };

  // Buffers directly addressed from PushConstant pointer fields.
  use(m_data->sceneInfoGpuBuffer,      vsfs);
  use(m_data->sceneResourcesGpuBuffer, vsfs);

  // Buffers indirectly addressed via SceneResources pointer fields.
  use(m_data->instancesGpuBuffer,      vsfs);
  use(m_data->meshPrimitivesGpuBuffer, vsfs);
  use(m_data->materialsGpuBuffer,      vsfs);

  // Raw mesh-data buffers addressed via MeshPrimitive.buffer.
  // These are the leaf nodes of the pointer chain; the vertex shader reads
  // positions, normals and UVs from them.
  for (auto &[bufferId, buf] : m_data->rawBuffers) {
    use(buf, vs);
  }
}

/**********************************************************/
bool MetalDeviceAssets::addAndUploadTexture(const core::Image & /*image*/,
                                            TextureID &id,
                                            bool /*immediate*/)
/**********************************************************/
{
  id = 0;
  return false; // Textures not yet implemented for Metal renderer
}

#endif // __APPLE__
