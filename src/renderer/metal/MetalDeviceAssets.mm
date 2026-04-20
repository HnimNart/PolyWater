#ifdef __APPLE__

#import "MetalDeviceAssets.hpp"

#import <Metal/Metal.h>

#import "backend/metal/core/MetalContextManager.hpp"
#import "scene/Scene.h"
#import "shaders/shared/structs.h"

// ---------------------------------------------------------------------------
// Interleaved vertex layout used by the Metal rasterisation shaders.
// ---------------------------------------------------------------------------
struct MetalVertex {
  float position[3]; // x, y, z
  float normal[3];   // nx, ny, nz
};

// Per-mesh Metal GPU resources built from the raw mesh bytes.
struct MetalMeshBuffer {
  id<MTLBuffer> vertexBuffer;
  id<MTLBuffer> indexBuffer;
  uint32_t indexCount  = 0;
  bool     is32Bit     = false;
};

struct MetalDeviceAssetsData {
  id<MTLDevice> device;

  // Raw upload buffers (one per upload() call)
  std::unordered_map<IDeviceAssets::BufferID, id<MTLBuffer>> rawBuffers;

  // mesh ID → buffer ID (set by linkMeshToBuffer)
  std::unordered_map<IDeviceAssets::MeshID, IDeviceAssets::BufferID>
      meshToBuffer;

  // mesh ID → ready-to-draw Metal buffers (built in uploadSceneResoures)
  std::unordered_map<IDeviceAssets::MeshID, MetalMeshBuffer> meshBuffers;

  IDeviceAssets::BufferID nextId = 1;
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

  IDeviceAssets::BufferID id = m_data->nextId++;
  m_data->rawBuffers[id]     = buf;

  // Metal has no BDA; address is left nullptr intentionally.
  return BufferHandle{.address = nullptr, .id = id};
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
  m_data->meshToBuffer[meshId] = bufferId;
}

/**********************************************************/
void MetalDeviceAssets::uploadSceneResoures(const Scene &resources)
/**********************************************************/
{
  // For every mesh, build an interleaved vertex buffer and a compact
  // index buffer from the raw data already uploaded to rawBuffers.
  for (uint32_t meshIdx = 0; meshIdx < resources.meshes.size(); ++meshIdx) {
    const shaderio::MeshPrimitive &prim = resources.meshes[meshIdx];
    if (meshIdx >= resources.meshData.size()) {
      continue;
    }
    const std::vector<uint8_t> &rawData = resources.meshData[prim.rawBufferIndex];
    if (rawData.empty()) {
      continue;
    }

    const shaderio::TriangleMesh &tri = prim.triMesh;
    const uint32_t vertexCount = tri.positions.count;
    const uint32_t indexCount  = tri.indices.count;
    if (vertexCount == 0 || indexCount == 0) {
      continue;
    }

    // ----------------------------------------------------------------
    // Build interleaved vertex buffer: float3 position + float3 normal
    // ----------------------------------------------------------------
    const uint32_t posStride  = tri.positions.byteStride == 0
                                    ? 12u
                                    : tri.positions.byteStride;
    const uint32_t normStride = tri.normals.byteStride == 0
                                    ? 12u
                                    : tri.normals.byteStride;
    const bool hasNormals = tri.normals.count > 0;

    std::vector<MetalVertex> vertices(vertexCount);
    for (uint32_t v = 0; v < vertexCount; ++v) {
      // Position
      const uint8_t *posPtr =
          rawData.data() + tri.positions.offset + v * posStride;
      std::memcpy(vertices[v].position, posPtr, sizeof(float) * 3);

      // Normal (default upward if mesh has none)
      if (hasNormals) {
        const uint8_t *normPtr =
            rawData.data() + tri.normals.offset + v * normStride;
        std::memcpy(vertices[v].normal, normPtr, sizeof(float) * 3);
      } else {
        vertices[v].normal[0] = 0.0f;
        vertices[v].normal[1] = 1.0f;
        vertices[v].normal[2] = 0.0f;
      }
    }

    id<MTLBuffer> vb = [m_data->device
        newBufferWithBytes:vertices.data()
                   length:vertices.size() * sizeof(MetalVertex)
                  options:MTLResourceStorageModeShared];

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
    mb.vertexBuffer = vb;
    mb.indexBuffer  = ib;
    mb.indexCount   = indexCount;
    mb.is32Bit      = is32Bit;

    m_data->meshBuffers[meshIdx] = mb;
  }
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
void *MetalDeviceAssets::getVertexMetalBuffer(MeshID meshId) const
/**********************************************************/
{
  auto it = m_data->meshBuffers.find(meshId);
  if (it == m_data->meshBuffers.end()) {
    return nullptr;
  }
  return (__bridge void *)it->second.vertexBuffer;
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
bool MetalDeviceAssets::addAndUploadTexture(const core::Image & /*image*/,
                                            TextureID &id,
                                            bool /*immediate*/)
/**********************************************************/
{
  id = 0;
  return false; // Textures not yet implemented for Metal renderer
}

#endif // __APPLE__
