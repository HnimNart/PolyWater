#pragma once

#include <shaders/shaderio.h>

#include <memory>
#include <nvshaders_host/sky.hpp>
#include <nvshaders_host/tonemapper.hpp>
#include <nvslang/slang.hpp>
#include <nvutils/camera_manipulator.hpp>
#include <nvvk/acceleration_structures.hpp>  // Acceleration structure management
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>  // GBuffer management
#include <nvvk/graphics_pipeline.hpp>
#include <nvvk/sampler_pool.hpp>
#include <nvvk/sbt_generator.hpp>

#include "_autogen/foundation.slang.h"  // Local shader
#include "_autogen/rtbasic.slang.h"     // Local shader
#include "_autogen/sky_simple.slang.h"  // from nvpro_core2
#include "_autogen/tonemapper.slang.h"  //   "    "
#include "backend/vulkan/utils.hpp"     // Common utilities for the sample application
#include "common/path_utils.hpp"        // Path utilities for handling resources file paths
#include "nvvk/default_structs.hpp"
#include "nvvk/formats.hpp"
#include "scene/gltf/gltf_utils.hpp"  // GLTF utilities for loading and importing GLTF models
#include "scene_context.hpp"
#include "shaders/compiler/slang.hpp"

class SceneManager
{
public:
  // Type of GBuffers
  enum
  {
    eImgRendered,
    eImgTonemapped
  };

  SceneManager(VulkanContext* ctx)
  {
    m_ctx = ctx;
    // The VMA allocator is used for all allocations, the staging uploader will use it for staging
    // buffers and images
    m_stagingUploader.init(m_ctx->allocator, true);

    // Acquiring the texture sampler which will be used for displaying the GBuffer
    m_samplerPool.init(m_ctx->device);

    VkSampler linearSampler{};
    NVVK_CHECK(m_samplerPool.acquireSampler(linearSampler));
    NVVK_DBG_NAME(linearSampler);

    // Create the G-Buffers
    nvvk::GBufferInitInfo gBufferInit{
        .allocator = m_ctx->allocator,
        .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT,
                         VK_FORMAT_R8G8B8A8_UNORM},  // Render target, tonemapped
        .depthFormat = nvvk::findDepthFormat(m_ctx->physicalDevice),
        .imageSampler = linearSampler,
        .descriptorPool = m_ctx->textureDescriptorPool,
    };
    m_gBuffers.init(gBufferInit);

    createGraphicsDescriptorSetLayout();
    createGraphicsPipelineLayout();
    compileAndCreateGraphicsShaders();
  }

  void clear()
  {
    m_descPack.deinit();

    for (auto& texture : m_textures)
    {
      m_ctx->allocator->destroyImage(texture);
    }

    vkDestroyPipelineLayout(m_ctx->device, m_graphicPipelineLayout, nullptr);
    vkDestroyShaderEXT(m_ctx->device, m_vertexShader, nullptr);
    vkDestroyShaderEXT(m_ctx->device, m_fragmentShader, nullptr);

    m_ctx->allocator->destroyBuffer(m_sceneResource.bSceneInfo);
    m_ctx->allocator->destroyBuffer(m_sceneResource.bMeshes);
    m_ctx->allocator->destroyBuffer(m_sceneResource.bMaterials);
    m_ctx->allocator->destroyBuffer(m_sceneResource.bInstances);
    for (auto& gltfData : m_sceneResource.bGltfDatas)
    {
      m_ctx->allocator->destroyBuffer(gltfData);
    }

    m_skySimple.deinit();
    m_tonemapper.deinit();
    m_gBuffers.deinit();
    m_stagingUploader.deinit();
    m_samplerPool.deinit();

    // Cleanup acceleration structures
    vkDestroyPipelineLayout(m_ctx->device, m_rtPipelineLayout, nullptr);
    vkDestroyPipeline(m_ctx->device, m_rtPipeline, nullptr);
    m_rtDescPack.deinit();
    m_ctx->allocator->destroyBuffer(m_sbtBuffer);

    m_asBuilder.deinitAccelerationStructures();
    m_asBuilder.deinit();
    m_sbtGenerator.deinit();
  }

  void initPost()
  {
    updateTextures();

    // Initialize the Sky with the pre-compiled shader
    m_skySimple.init(m_ctx->allocator, std::span(sky_simple_slang));

    // Initialize the tonemapper also with proe-compiled shader
    m_tonemapper.init(m_ctx->allocator, std::span(tonemapper_slang));

    // Get ray tracing properties
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    prop2.pNext = &m_rtProperties;
    vkGetPhysicalDeviceProperties2(m_ctx->physicalDevice, &prop2);

    // Initialize acceleration structure builder
    m_asBuilder.init(m_ctx->allocator, &m_stagingUploader, m_ctx->graphicsQueue);

    // Initialize SBT generator
    m_sbtGenerator.init(m_ctx->device, m_rtProperties);

    // Set up acceleration structure infrastructure
    createBottomLevelAS();  // Set up BLAS infrastructure
    createTopLevelAS();     // Set up TLAS infrastructure
    // Set up ray tracing pipeline infrastructure
    createRaytraceDescriptorLayout();  // Create descriptor layout
    createRayTracingPipeline();        // Create pipeline structure and SBT
  }

  //---------------------------------------------------------------------------------------------------------------
  // Create the scene for this sample
  // - Load a teapot, a plane and an image.
  // - Create instances for them, assign a material and a transformation
  void createScene(VkCommandBuffer cmd)
  {
    SCOPED_TIMER(__FUNCTION__);
    // Load the GLTF resources
    {
      tinygltf::Model teapotModel = nvsamples::loadGltfResources(nvutils::findFile(
          "teapot.gltf",
          nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      tinygltf::Model planeModel = nvsamples::loadGltfResources(nvutils::findFile(
          "plane.gltf", nvsamples::getResourcesDirs()));  // Load the GLTF resources from the file

      // Textures
      {
        std::filesystem::path imageFilename =
            nvutils::findFile("tiled_floor.png", nvsamples::getResourcesDirs());
        nvvk::Image texture = nvsamples::loadAndCreateImage(
            cmd, m_stagingUploader, m_ctx->device,
            imageFilename);  // Load the image from the file and create a texture from it
        NVVK_DBG_NAME(texture.image);
        m_samplerPool.acquireSampler(texture.descriptor.sampler);
        m_textures.emplace_back(texture);  // Store the texture in the vector of textures
      }

      // Upload the GLTF resources to the GPU
      {
        nvsamples::importGltfData(m_sceneResource, teapotModel,
                                  m_stagingUploader);  // Import the GLTF resources
        nvsamples::importGltfData(m_sceneResource, planeModel,
                                  m_stagingUploader);  // Import the GLTF resources
      }
    }

    m_sceneResource.materials = {// Teapot material
                                 {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f),
                                  .metallicFactor = 0.5f,
                                  .roughnessFactor = 0.5f},
                                 // Plane material with texture
                                 {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                  .metallicFactor = 0.1f,
                                  .roughnessFactor = 0.8f,
                                  .baseColorTextureIndex = 1}};

    m_sceneResource.instances = {
        // Teapot
        {.transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)) *
                      glm::scale(glm::mat4(1), glm::vec3(0.5f)),
         .materialIndex = 0,
         .meshIndex = 0},
        // Plane
        {.transform =
             glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -0.9f, 0)), glm::vec3(2.f)),
         .materialIndex = 1,
         .meshIndex = 1},
    };

    nvsamples::createGltfSceneInfoBuffer(
        m_sceneResource,
        m_stagingUploader);  // Create buffers for the scene data (GPU buffers)

    m_stagingUploader.cmdUploadAppended(cmd);  // Upload the scene information to the GPU

    // Scene information
    shaderio::GltfSceneInfo& sceneInfo = m_sceneResource.sceneInfo;
    sceneInfo.useSky = false;  // Use light
    sceneInfo.instances = (shaderio::GltfInstance*)
                              m_sceneResource.bInstances.address;  // Address of the instance buffer
    sceneInfo.meshes =
        (shaderio::GltfMesh*) m_sceneResource.bMeshes.address;  // Address of the mesh buffer
    sceneInfo.materials = (shaderio::GltfMetallicRoughness*)
                              m_sceneResource.bMaterials.address;  // Address of the material buffer
    sceneInfo.backgroundColor = {0.85f, 0.85f, 0.85f};             // The background color
    sceneInfo.numLights = 1;
    sceneInfo.punctualLights[0].color = glm::vec3(1.0f, 1.0f, 1.0f);
    sceneInfo.punctualLights[0].intensity = 4.0f;
    sceneInfo.punctualLights[0].position = glm::vec3(1.0f, 1.0f, 1.0f);   // Position of the light
    sceneInfo.punctualLights[0].direction = glm::vec3(1.0f, 1.0f, 1.0f);  // Direction to the light
    sceneInfo.punctualLights[0].type = shaderio::GltfLightType::ePoint;
    sceneInfo.punctualLights[0].coneAngle =
        0.9f;  // Cone angle for spot lights (0 for point and directional lights)

    // Default camera
    m_cameraManip->setClipPlanes({0.01F, 100.0F});
    m_cameraManip->setLookat({0.0F, 0.5F, 5.0}, {0.F, 0.F, 0.F}, {0.0F, 1.0F, 0.0F});
  }

  void create_ray_tracing_pipeline()
  {
    // Set up acceleration structure infrastructure
    createBottomLevelAS();  // Set up BLAS infrastructure
    createTopLevelAS();     // Set up TLAS infrastructure

    // Set up ray tracing pipeline infrastructure
    createRaytraceDescriptorLayout();  // Create descriptor layout
    createRayTracingPipeline();        // Create pipeline structure and SBT
  }
  //---------------------------------------------------------------------------------------------------------------
  // Create bottom-level acceleration structures
  void createBottomLevelAS()
  {
    SCOPED_TIMER(__FUNCTION__);

    // Prepare geometry information for all meshes
    std::vector<nvvk::AccelerationStructureGeometryInfo> geoInfos(resources().meshes.size());
    for (uint32_t p_idx = 0; p_idx < resources().meshes.size(); p_idx++)
    {
      geoInfos[p_idx] = primitiveToGeometry(resources().meshes[p_idx]);
    }

    // Build the bottom-level acceleration structures
    m_asBuilder.blasSubmitBuildAndWait(geoInfos,
                                       VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  //--------------------------------------------------------------------------------------------------
  // Converting a PrimitiveMesh as input for BLAS
  //
  nvvk::AccelerationStructureGeometryInfo primitiveToGeometry(const shaderio::GltfMesh& gltfMesh)
  {
    nvvk::AccelerationStructureGeometryInfo result = {};

    const shaderio::TriangleMesh triMesh = gltfMesh.triMesh;
    const auto triangleCount = static_cast<uint32_t>(triMesh.indices.count / 3U);

    // Describe buffer as array of VertexObj.
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,  // vec3 vertex position data
        .vertexData = {.deviceAddress =
                           VkDeviceAddress(gltfMesh.gltfBuffer) + triMesh.positions.offset},
        .vertexStride = triMesh.positions.byteStride,
        .maxVertex = triMesh.positions.count - 1,
        .indexType = VkIndexType(
            gltfMesh.indexType),  // Index type (VK_INDEX_TYPE_UINT16 or VK_INDEX_TYPE_UINT32)
        .indexData = {.deviceAddress =
                          VkDeviceAddress(gltfMesh.gltfBuffer) + triMesh.indices.offset},
    };

    // Identify the above data as containing opaque triangles.
    result.geometry = VkAccelerationStructureGeometryKHR{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry = {.triangles = triangles},
        .flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR | VK_GEOMETRY_OPAQUE_BIT_KHR,
    };

    result.rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{.primitiveCount = triangleCount};

    return result;
  }

  //--------------------------------------------------------------------------------------------------
  // Create the top level acceleration structures, referencing all BLAS
  //
  void createTopLevelAS()
  {
    SCOPED_TIMER(__FUNCTION__);

    // Prepare instance data for TLAS
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(resources().instances.size());
    const VkGeometryInstanceFlagsKHR flags{VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};

    for (const shaderio::GltfInstance& instance : resources().instances)
    {
      VkAccelerationStructureInstanceKHR ray_inst{};
      ray_inst.transform =
          nvvk::toTransformMatrixKHR(instance.transform);  // Position of the instance
      ray_inst.instanceCustomIndex = instance.meshIndex;   // gl_InstanceCustomIndexEXT
      ray_inst.accelerationStructureReference = m_asBuilder.blasSet[instance.meshIndex].address;
      ray_inst.instanceShaderBindingTableRecordOffset =
          0;  // We will use the same hit group for all objects
      ray_inst.flags = flags;
      ray_inst.mask = 0xFF;
      tlasInstances.emplace_back(ray_inst);
    }

    // Build the top-level acceleration structure
    m_asBuilder.tlasSubmitBuildAndWait(tlasInstances,
                                       VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
  }

  //--------------------------------------------------------------------------------------------------
  // Create the descriptor set layout for ray tracing
  void createRaytraceDescriptorLayout()
  {
    SCOPED_TIMER(__FUNCTION__);
    nvvk::DescriptorBindings bindings;
    bindings.addBinding({.binding = shaderio::BindingPoints::eTlas,
                         .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                         .descriptorCount = 1,
                         .stageFlags = VK_SHADER_STAGE_ALL});
    bindings.addBinding({.binding = shaderio::BindingPoints::eOutImage,
                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                         .descriptorCount = 1,
                         .stageFlags = VK_SHADER_STAGE_ALL});

    // Creating a PUSH descriptor set and set layout from the bindings
    m_rtDescPack.init(bindings, m_ctx->device, 0,
                      VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
  }

  //--------------------------------------------------------------------------------------------------
  // Create ray tracing pipeline structure
  // We create the entries for ray generation, miss, and closest hit shaders.
  // We also create the shader groups and the pipeline layout.
  // The pipeline is used to execute the ray tracing pipeline.
  // We also create the SBT (Shader Binding Table)
  void createRayTracingPipeline()
  {
    SCOPED_TIMER(__FUNCTION__);
    // For re-creation
    vkDestroyPipeline(m_ctx->device, m_rtPipeline, nullptr);
    vkDestroyPipelineLayout(m_ctx->device, m_rtPipelineLayout, nullptr);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      eShaderGroupCount
    };
    std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};
    for (auto& s : stages)
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    // Compile shader, fallback to pre-compiled
    VkShaderModuleCreateInfo shaderCode = m_compiler.compile("rtbasic.slang", rtbasic_slang);

    stages[eRaygen].pNext = &shaderCode;
    stages[eRaygen].pName = "rgenMain";
    stages[eRaygen].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[eMiss].pNext = &shaderCode;
    stages[eMiss].pName = "rmissMain";
    stages[eMiss].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[eClosestHit].pNext = &shaderCode;
    stages[eClosestHit].pName = "rchitMain";
    stages[eClosestHit].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Shader groups
    VkRayTracingShaderGroupCreateInfoKHR group{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.anyHitShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = VK_SHADER_UNUSED_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;
    // Raygen
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shader_groups.push_back(group);

    // Miss
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eMiss;
    shader_groups.push_back(group);

    // closest hit shader
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shader_groups.push_back(group);

    // Push constant: we want to be able to update constants used by the shaders
    const VkPushConstantRange push_constant{VK_SHADER_STAGE_ALL, 0,
                                            sizeof(shaderio::TutoPushConstant)};

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant;

    // Descriptor sets: one specific to ray tracing, and one shared with the rasterization pipeline
    std::array<VkDescriptorSetLayout, 2> layouts = {
        {m_descPack.getLayout(), m_rtDescPack.getLayout()}};
    pipeline_layout_create_info.setLayoutCount = uint32_t(layouts.size());
    pipeline_layout_create_info.pSetLayouts = layouts.data();
    vkCreatePipelineLayout(m_ctx->device, &pipeline_layout_create_info, nullptr,
                           &m_rtPipelineLayout);
    NVVK_DBG_NAME(m_rtPipelineLayout);

    // Assemble the shader stages and recursion depth info into the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    rtPipelineInfo.stageCount = static_cast<uint32_t>(stages.size());  // Stages are shaders
    rtPipelineInfo.pStages = stages.data();
    rtPipelineInfo.groupCount = static_cast<uint32_t>(shader_groups.size());
    rtPipelineInfo.pGroups = shader_groups.data();
    rtPipelineInfo.maxPipelineRayRecursionDepth =
        std::max(3U, m_rtProperties.maxRayRecursionDepth);  // Ray depth
    rtPipelineInfo.layout = m_rtPipelineLayout;
    vkCreateRayTracingPipelinesKHR(m_ctx->device, {}, {}, 1, &rtPipelineInfo, nullptr,
                                   &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);

    // Create the shader binding table for this pipeline
    createShaderBindingTable(rtPipelineInfo);
  }

  //---------------------------------------------------------------------------------------------------------------
  // Ray tracing rendering method
  void raytraceScene(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Ray trace pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);

    // Bind the descriptor sets for the graphics pipeline (making textures available to the shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
        .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
        .stageFlags = VK_SHADER_STAGE_ALL,
        .layout = m_rtPipelineLayout,
        .firstSet = 0,
        .descriptorSetCount = 1,
        .pDescriptorSets = m_descPack.getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

    // Push descriptor sets for ray tracing
    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eTlas), m_asBuilder.tlas);
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eOutImage),
                 m_gBuffers.getColorImageView(eImgRendered), VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 1,
                              write.size(), write.data());

    // Push constant information
    shaderio::TutoPushConstant pushValues{
        .sceneInfoAddress = (shaderio::GltfSceneInfo*) resources().bSceneInfo.address,
        .metallicRoughnessOverride = m_metallicRoughnessOverride,
    };
    const VkPushConstantsInfo pushInfo{.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size = sizeof(shaderio::TutoPushConstant),
                                       .pValues = &pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    // Ray trace
    const nvvk::SBTGenerator::Regions& regions = m_sbtGenerator.getSBTRegions();
    const VkExtent2D& size = m_ctx->viewportSize;
    vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit, &regions.callable,
                      size.width, size.height, 1);

    // Barrier to make sure the image is ready for Tonemapping
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

  void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo)
  {
    SCOPED_TIMER(__FUNCTION__);

    m_ctx->allocator->destroyBuffer(m_sbtBuffer);  // Cleanup when re-creating
    // Calculate required SBT buffer size
    size_t bufferSize = m_sbtGenerator.calculateSBTBufferSize(m_rtPipeline, rtPipelineInfo);

    // Create SBT buffer using the size from above
    NVVK_CHECK(m_ctx->allocator->createBuffer(
        m_sbtBuffer, bufferSize, VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        m_sbtGenerator.getBufferAlignment()));
    NVVK_DBG_NAME(m_sbtBuffer.buffer);

    // Populate the SBT buffer with shader handles and data using the CPU-mapped memory pointer
    NVVK_CHECK(
        m_sbtGenerator.populateSBTBuffer(m_sbtBuffer.address, bufferSize, m_sbtBuffer.mapping));
  }

  //---------------------------------------------------------------------------------------------------------------
  // The Vulkan descriptor set defines the resources that are used by the shaders.
  // Here we add the bindings for the textures.
  void createGraphicsDescriptorSetLayout()
  {
    nvvk::DescriptorBindings bindings;
    bindings.addBinding({.binding = shaderio::BindingPoints::eTextures,
                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         .descriptorCount = 10,  // Maximum number of textures used in the scene
                         .stageFlags = VK_SHADER_STAGE_ALL},
                        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
    // Creating the descriptor set and set layout from the bindings
    m_descPack.init(bindings, m_ctx->device, 1,
                    VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    NVVK_DBG_NAME(m_descPack.getLayout());
    NVVK_DBG_NAME(m_descPack.getPool());
    NVVK_DBG_NAME(m_descPack.getSet(0));
  }

  //--------------------------------------------------------------------------------------------------
  // The graphic pipeline is all the stages that are used to render a section of the scene.
  // Stages like: vertex shader, fragment shader, rasterization, and blending.
  //
  void createGraphicsPipelineLayout()
  {
    // Push constant is used to pass data to the shader at each frame
    const VkPushConstantRange pushConstantRange{.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                                .offset = 0,
                                                .size = sizeof(shaderio::TutoPushConstant)};

    // The pipeline layout is used to pass data to the pipeline, anything with "layout" in the
    // shader
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = m_descPack.getLayoutPtr(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    NVVK_CHECK(vkCreatePipelineLayout(m_ctx->device, &pipelineLayoutInfo, nullptr,
                                      &m_graphicPipelineLayout));
    NVVK_DBG_NAME(m_graphicPipelineLayout);
  }

  //--------------------------------------------------------------------------------------------------
  // Update the textures: this is called when the scene is loaded
  // Textures are updated in the descriptor set (0)
  void updateTextures()
  {
    if (m_textures.empty())
      return;

    // Update the descriptor set with the textures
    nvvk::WriteSetContainer write{};
    VkWriteDescriptorSet allTextures =
        m_descPack.makeWrite(shaderio::BindingPoints::eTextures, 0, 1, uint32_t(m_textures.size()));
    nvvk::Image* allImages = m_textures.data();
    write.append(allTextures, allImages);
    vkUpdateDescriptorSets(m_ctx->device, write.size(), write.data(), 0, nullptr);
  }

  //---------------------------------------------------------------------------------------------------------------
  // Compile the graphics shaders and create the shader modules.
  // This function only creates vertex and fragment shader modules for the graphics pipeline.
  // The actual graphics pipeline is created elsewhere and uses these shader modules.
  // This function will use the pre-compiled shaders if the compilation fails.
  void compileAndCreateGraphicsShaders()
  {
    SCOPED_TIMER(__FUNCTION__);

    // Use pre-compiled shaders by default
    VkShaderModuleCreateInfo shaderCode = m_compiler.compile("foundation.slang", foundation_slang);

    // Destroy the previous shaders if they exist
    vkDestroyShaderEXT(m_ctx->device, m_vertexShader, nullptr);
    vkDestroyShaderEXT(m_ctx->device, m_fragmentShader, nullptr);

    // Push constant is used to pass data to the shader at each frame
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset = 0,
        .size = sizeof(shaderio::TutoPushConstant),
    };

    // Shader create information, this is used to create the shader modules
    VkShaderCreateInfoEXT shaderInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .pName = "main",
        .setLayoutCount = 1,
        .pSetLayouts = m_descPack.getLayoutPtr(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };

    // Vertex Shader
    shaderInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderInfo.pName = "vertexMain";  // The entry point of the vertex shader
    shaderInfo.codeSize = shaderCode.codeSize;
    shaderInfo.pCode = shaderCode.pCode;
    vkCreateShadersEXT(m_ctx->device, 1U, &shaderInfo, nullptr, &m_vertexShader);
    NVVK_DBG_NAME(m_vertexShader);

    // Fragment Shader
    shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderInfo.nextStage = 0;
    shaderInfo.pName = "fragmentMain";  // The entry point of the vertex shader
    shaderInfo.codeSize = shaderCode.codeSize;
    shaderInfo.pCode = shaderCode.pCode;
    vkCreateShadersEXT(m_ctx->device, 1U, &shaderInfo, nullptr, &m_fragmentShader);
    NVVK_DBG_NAME(m_fragmentShader);
  }

  void post_process(VkCommandBuffer cmd)
  {
    // Default post-processing: tonemapping
    // TODO
    m_tonemapper.runCompute(cmd, m_gBuffers.getSize(), m_tonemapperData,
                            m_gBuffers.getDescriptorImageInfo(0),
                            m_gBuffers.getDescriptorImageInfo(1));
  }

  // Raster //
  //---------------------------------------------------------------------------------------------------------------
  // Recording the commands to render the scene
  //
  void rasterScene(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Push constant information, see usage later
    shaderio::TutoPushConstant pushValues{
        .sceneInfoAddress =
            (shaderio::GltfSceneInfo*) resources()
                .bSceneInfo
                .address,  // Pass the address of the scene information buffer to the shader
        .metallicRoughnessOverride =
            m_metallicRoughnessOverride,  // Override the metallic and roughness values
    };
    const VkPushConstantsInfo pushInfo{
        .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
        .layout = m_graphicPipelineLayout,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset = 0,
        .size = sizeof(shaderio::TutoPushConstant),
        .pValues = &pushValues,  // Other values are passed later
    };

    // Rendering the Sky
    if (resources().sceneInfo.useSky)
    {
      const glm::mat4& viewMatrix = camera()->getViewMatrix();
      const glm::mat4& projMatrix = camera()->getPerspectiveMatrix();
      m_skySimple.runCompute(cmd, m_ctx->viewportSize, viewMatrix, projMatrix,
                             resources().sceneInfo.skySimpleParam,
                             m_gBuffers.getDescriptorImageInfo(0));
    }

    // Rendering to the GBuffer
    VkRenderingAttachmentInfo colorAttachment = DEFAULT_VkRenderingAttachmentInfo;
    colorAttachment.loadOp =
        resources().sceneInfo.useSky
            ? VK_ATTACHMENT_LOAD_OP_LOAD
            : VK_ATTACHMENT_LOAD_OP_CLEAR;  // Load the previous content of the GBuffer color
                                            // attachment (Sky rendering)
    colorAttachment.imageView = m_gBuffers.getColorImageView(0);
    colorAttachment.clearValue = {.color = {resources().sceneInfo.backgroundColor.x,
                                            resources().sceneInfo.backgroundColor.y,
                                            resources().sceneInfo.backgroundColor.z, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment = DEFAULT_VkRenderingAttachmentInfo;
    depthAttachment.imageView = m_gBuffers.getDepthImageView();
    depthAttachment.clearValue = {.depthStencil = DEFAULT_VkClearDepthStencilValue};

    // Create the rendering info
    VkRenderingInfo renderingInfo = DEFAULT_VkRenderingInfo;
    renderingInfo.renderArea = DEFAULT_VkRect2D(m_gBuffers.getSize());
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    // Change the GBuffer layout to prepare for rendering (attachment)
    nvvk::cmdImageMemoryBarrier(cmd,
                                {m_gBuffers.getColorImage(eImgRendered), VK_IMAGE_LAYOUT_GENERAL,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    // Bind the descriptor sets for the graphics pipeline (making textures available to the shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
        .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .layout = m_graphicPipelineLayout,
        .firstSet = 0,
        .descriptorSetCount = 1,
        .pDescriptorSets = m_descPack.getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

    // ** BEGIN RENDERING **
    vkCmdBeginRendering(cmd, &renderingInfo);

    // All dynamic states are set here
    m_dynamicPipeline.rasterizationState.cullMode =
        VK_CULL_MODE_NONE;  // Don't cull any triangles (double-sided rendering)
    m_dynamicPipeline.cmdApplyAllStates(cmd);
    m_dynamicPipeline.cmdSetViewportAndScissor(cmd, m_ctx->viewportSize);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);

    // Same shader for all meshes
    m_dynamicPipeline.cmdBindShaders(cmd, {.vertex = m_vertexShader, .fragment = m_fragmentShader});

    // We don't send vertex attributes, they are pulled in the shader
    VkVertexInputBindingDescription2EXT bindingDescription = {};
    VkVertexInputAttributeDescription2EXT attributeDescription = {};
    vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

    for (size_t i = 0; i < resources().instances.size(); i++)
    {
      uint32_t meshIndex = resources().instances[i].meshIndex;
      const shaderio::GltfMesh& gltfMesh = resources().meshes[meshIndex];
      const shaderio::TriangleMesh& triMesh = gltfMesh.triMesh;

      // Push constant is information that is passed to the shader at each draw call.
      pushValues.normalMatrix =
          glm::transpose(glm::inverse(glm::mat3(resources().instances[i].transform)));
      pushValues.instanceIndex = int(i);  // The index of the instance in the m_instances vector
      vkCmdPushConstants2(cmd, &pushInfo);

      // Get the buffer directly using the pre-computed mapping
      uint32_t bufferIndex = resources().meshToBufferIndex[meshIndex];
      const nvvk::Buffer& v = resources().bGltfDatas[bufferIndex];

      // Bind index buffers
      vkCmdBindIndexBuffer(cmd, v.buffer, triMesh.indices.offset, VkIndexType(gltfMesh.indexType));

      // Draw the mesh
      vkCmdDrawIndexed(cmd, triMesh.indices.count, 1, 0, 0, 0);  // All indices
    }

    // ** END RENDERING **
    vkCmdEndRendering(cmd);
    nvvk::cmdImageMemoryBarrier(cmd, {m_gBuffers.getColorImage(eImgRendered),
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_GENERAL});
  }

  VkImage get_image(int buffer_idx) { return m_gBuffers.getColorImage(buffer_idx); }

  void onResize(VkCommandBuffer cmd, const VkExtent2D& size)
  {
    NVVK_CHECK(m_gBuffers.update(cmd, size));
  }

  std::shared_ptr<nvutils::CameraManipulator> camera() const { return m_cameraManip; }
  nvsamples::GltfSceneResource& resources() { return m_sceneResource; }
  const nvsamples::GltfSceneResource& resources() const { return m_sceneResource; }
  const nvvk::GBuffer& gbuffers() const { return m_gBuffers; }

  shaderio::TonemapperData& tonemapper() { return m_tonemapperData; }
  glm::vec2& metallic_roughness() { return m_metallicRoughnessOverride; }

  void set_camera(std::shared_ptr<nvutils::CameraManipulator> camera)
  {
    m_cameraManip = std::move(camera);
  }

public:
  VulkanContext* m_ctx = nullptr;

  nvvk::StagingUploader m_stagingUploader{};  // Utility to upload data to the GPU
  nvvk::SamplerPool m_samplerPool{};          // Texture sampler pool
  nvvk::GBuffer m_gBuffers{};                 // The G-Buffer
  // Camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> m_cameraManip{
      std::make_shared<nvutils::CameraManipulator>()};

  // Pipeline
  nvvk::GraphicsPipelineState
      m_dynamicPipeline;  // The dynamic pipeline state used to set the graphics pipeline state,
                          // like viewport, scissor, and depth test
  nvvk::DescriptorPack m_descPack;  // The descriptor bindings used to create the descriptor set
                                    // layout and descriptor sets
  VkPipelineLayout m_graphicPipelineLayout{};  // The pipeline layout use with graphics pipeline

  // Shaders
  VkShaderEXT m_vertexShader{};    // The vertex shader used to render the scene
  VkShaderEXT m_fragmentShader{};  // The fragment shader used to render the scene

  // Scene information buffer (UBO)
  nvsamples::GltfSceneResource m_sceneResource{};  // The GLTF scene resource, contains all the
                                                   // buffers and data for the scene
  std::vector<nvvk::Image> m_textures{};           // Textures used in the scene

  nvshaders::SkySimple m_skySimple{};    // Sky rendering
  nvshaders::Tonemapper m_tonemapper{};  // Tonemapper for post-processing effects
  shaderio::TonemapperData
      m_tonemapperData{};  // Tonemapper data used to pass parameters to the tonemapper shader
  glm::vec2 m_metallicRoughnessOverride{
      -0.01f, -0.01f};  // Override values for metallic and roughness, used in the UI to control
                        // the material properties

  // Ray Tracing Pipeline Components
  nvvk::DescriptorPack m_rtDescPack;      // Ray tracing descriptor bindings
  VkPipeline m_rtPipeline{};              // Ray tracing pipeline
  VkPipelineLayout m_rtPipelineLayout{};  // Ray tracing pipeline layout

  // Acceleration Structure Components
  nvvk::AccelerationStructureHelper m_asBuilder{};  // Helper to create acceleration structures
  nvvk::SBTGenerator m_sbtGenerator;                // Shader binding table wrapper
  nvvk::Buffer m_sbtBuffer;                         // Buffer for shader binding table

  // Ray Tracing Properties
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};

  // Slang compiler
  SlangShaderCompiler m_compiler{nvsamples::getShaderDirs()};
};