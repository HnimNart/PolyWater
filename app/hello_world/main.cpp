/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

// Enable the use of Nsight Aftermath for crash tracking and shader debugging
// #define USE_NSIGHT_AFTERMATH  // (not always on, as it slows down the application)

#define TINYGLTF_IMPLEMENTATION         // Implementation of the GLTF loader library
#define STB_IMAGE_IMPLEMENTATION        // Implementation of the image loading library
#define STB_IMAGE_WRITE_IMPLEMENTATION  // Implementation of the image writing library
#define VMA_DYNAMIC_VULKAN_FUNCTIONS                                                               \
  1                         // Use dynamic Vulkan functions for VMA (Vulkan Memory Allocator)
#define VMA_IMPLEMENTATION  // Implementation of the Vulkan Memory Allocator
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                           \
  {                                                                                                \
    printf((format), __VA_ARGS__);                                                                 \
    printf("\n");                                                                                  \
  }

#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nvaftermath/aftermath.hpp>     // Nsight Aftermath for crash tracking and shader debugging
#include <nvapp/application.hpp>         // Application framework
#include <nvapp/elem_camera.hpp>         // Camera manipulator
#include <nvapp/elem_default_menu.hpp>   // Default menu element
#include <nvapp/elem_default_title.hpp>  // Default title element
#include <nvgui/camera.hpp>              // Camera widget
#include <nvgui/sky.hpp>                 // Sky widget
#include <nvgui/tonemapper.hpp>          // Tonemapper widget
#include <nvutils/camera_manipulator.hpp>  // Camera manipulator
#include <nvutils/logger.hpp>              // Logger for debug messages
#include <nvutils/parameter_parser.hpp>    // Parameter parser
#include <nvutils/timers.hpp>              // Timers for profiling
#include <nvvk/context.hpp>                // Vulkan context management
#include <nvvk/validation_settings.hpp>    // Validation settings for Vulkan

#include "src/scene/manager.hpp"

VulkanContext* ctx = nullptr;

class RtBasic : public nvapp::IAppElement
{

public:
  RtBasic() = default;
  ~RtBasic() override = default;

  void setup_scene(VkCommandBuffer cmd, VulkanContext* vulkan_ctx)
  {
    CpuSceneResources& scene_resources = m_scene_manager->scene_resources();
    SCOPED_TIMER(__FUNCTION__);
    // Load the GLTF resources
    tinygltf::Model teapotModel = scene_resources.loadGltf("teapot.gltf");
    tinygltf::Model planeModel = scene_resources.loadGltf("plane.gltf");

    // Textures
    {
      scene_resources.loadTexture("tiled_floor.png", cmd);
    }

    // Teapot material
    CpuSceneResources::MaterialID teapot_id =
        scene_resources.addMaterial({.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f),
                                     .metallicFactor = 0.5f,
                                     .roughnessFactor = 0.5f});
    // Plane material with texture
    CpuSceneResources::MaterialID plane_id =
        scene_resources.addMaterial({.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                     .metallicFactor = 0.1f,
                                     .roughnessFactor = 0.8f,
                                     .baseColorTextureIndex = 1});

    // Teapot
    scene_resources.addInstance({.transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)) *
                                              glm::scale(glm::mat4(1), glm::vec3(0.5f)),
                                 .materialIndex = teapot_id,
                                 .meshIndex = 0});
    // Plane
    scene_resources.addInstance(
        {.transform =
             glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -0.9f, 0)), glm::vec3(2.f)),
         .materialIndex = plane_id,
         .meshIndex = 1});

    scene_resources.finalizeSceneResources(cmd);

    // Scene information
    nvsamples::GltfSceneResource& gltf_resources = m_scene_manager->gltf_resources();
    shaderio::GltfSceneInfo& sceneInfo = gltf_resources.sceneInfo;
    sceneInfo.useSky = false;  // Use light
    sceneInfo.instances = (shaderio::GltfInstance*)
                              gltf_resources.bInstances.address;  // Address of the instance buffer
    sceneInfo.meshes =
        (shaderio::GltfMesh*) gltf_resources.bMeshes.address;  // Address of the mesh buffer
    sceneInfo.materials = (shaderio::GltfMetallicRoughness*)
                              gltf_resources.bMaterials.address;  // Address of the material buffer
    sceneInfo.backgroundColor = {0.85f, 0.85f, 0.85f};            // The background color
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

  //-------------------------------------------------------------------------------
  // Create the what is needed
  // - Called when the application initialize
  void onAttach(nvapp::Application* app) override
  {
    m_app = app;

    // Initialize the VMA allocator
    VmaAllocatorCreateInfo allocatorInfo = {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = app->getPhysicalDevice(),
        .device = app->getDevice(),
        .instance = app->getInstance(),
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };
    m_allocator.init(allocatorInfo);
    // m_allocator.setLeakID(0);  // Set a leak ID for the allocator to track memory leaks
    // The VMA allocator is used for all allocations, the staging uploader will use it for staging
    // buffers and images
    m_stagingUploader.init(&m_allocator, true);

    ctx = new VulkanContext{.allocator = &m_allocator,
                            .physicalDevice = m_app->getPhysicalDevice(),
                            .device = m_app->getDevice(),
                            .graphicsQueue = m_app->getQueue(0),
                            .viewportSize = m_app->getViewportSize(),
                            .textureDescriptorPool = m_app->getTextureDescriptorPool(),
                            .stagingUploader = m_stagingUploader};

    m_vulkan_backend = std::make_shared<VulkanBackend>(ctx);
    m_scene_manager = std::make_unique<SceneManager>(ctx, m_vulkan_backend);
    m_scene_manager->set_camera(m_cameraManip);

    // Create scene
    VkCommandBuffer cmd = m_app->createTempCmdBuffer();
    setup_scene(cmd, ctx);
    m_app->submitAndWaitTempCmdBuffer(cmd);  // Submit the command buffer to upload the resources
    m_scene_manager->postInit();
  }

  //-------------------------------------------------------------------------------
  // Destroy all elements that were created
  // - Called when the application is shutting down
  //
  void onDetach() override
  {
    NVVK_CHECK(vkQueueWaitIdle(m_app->getQueue(0).queue));

    VkDevice device = m_app->getDevice();
    m_scene_manager->clear(ctx);
    m_stagingUploader.deinit();
    m_allocator.deinit();
  }

  //---------------------------------------------------------------------------------------------------------------
  // Rendering all UI elements, this includes the image of the GBuffer, the camera controls, and the
  // sky parameters.
  // - Called every frame
  void onUIRender() override
  {
    namespace PE = nvgui::PropertyEditor;
    // Display the rendering GBuffer in the ImGui window ("Viewport")
    if (ImGui::Begin("Viewport"))
    {
      ImGui::Image(ImTextureID(m_vulkan_backend->gbuffers().getDescriptorSet(eImgTonemapped)),
                   ImGui::GetContentRegionAvail());
    }
    ImGui::End();

    // Setting panel
    if (ImGui::Begin("Settings"))
    {
      // Ray tracing toggle
      ImGui::Checkbox("Use Ray Tracing", &m_useRayTracing);

      if (ImGui::CollapsingHeader("Camera"))
        nvgui::CameraWidget(m_scene_manager->camera());
      if (ImGui::CollapsingHeader("Environment"))
      {
        ImGui::Checkbox("Use Sky", (bool*) &m_scene_manager->gltf_resources().sceneInfo.useSky);
        if (m_scene_manager->gltf_resources().sceneInfo.useSky)
          nvgui::skySimpleParametersUI(m_scene_manager->gltf_resources().sceneInfo.skySimpleParam);
        else
        {
          PE::begin();
          PE::ColorEdit3("Background",
                         (float*) &m_scene_manager->gltf_resources().sceneInfo.backgroundColor);
          PE::end();
          // Light
          PE::begin();
          if (m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
                  shaderio::GltfLightType::ePoint ||
              m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
                  shaderio::GltfLightType::eSpot)
          {
            PE::DragFloat3(
                "Light Position",
                glm::value_ptr(
                    m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].position),
                1.0f, -20.0f, 20.0f, "%.2f", ImGuiSliderFlags_None, "Position of the light");
          }
          if (m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
                  shaderio::GltfLightType::eDirectional ||
              m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
                  shaderio::GltfLightType::eSpot)
          {
            PE::SliderFloat3(
                "Light Direction",
                glm::value_ptr(
                    m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].direction),
                -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_None, "Direction of the light");
          }

          PE::SliderFloat("Light Intensity",
                          &m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].intensity,
                          0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic,
                          "Intensity of the light");
          PE::ColorEdit3(
              "Light Color",
              glm::value_ptr(m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].color),
              ImGuiColorEditFlags_NoInputs, "Color of the light");
          PE::Combo("Light Type",
                    (int*) &m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type,
                    "Point\0Spot\0Directional\0", 3,
                    "Type of the light (Point, Spot, Directional)");
          if (m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
              shaderio::GltfLightType::eSpot)
          {
            PE::SliderAngle(
                "Cone Angle",
                &m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].coneAngle, 0.f, 90.f,
                "%.2f", ImGuiSliderFlags_AlwaysClamp, "Cone angle of the spot light");
          }
          PE::end();
        }
      }
      if (ImGui::CollapsingHeader("Tonemapper"))
      {
        nvgui::tonemapperWidget(m_scene_manager->tonemapper());
      }
      ImGui::Separator();
      PE::begin();
      PE::SliderFloat2("Metallic/Roughness Override",
                       glm::value_ptr(m_scene_manager->metallic_roughness()), -0.01f, 1.0f, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp,
                       "Override all material metallic and roughness");
      PE::end();
    }
    ImGui::End();
  }

  //---------------------------------------------------------------------------------------------------------------
  // When the viewport is resized, the GBuffer must be resized
  // - Called when the Window "viewport is resized
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size) override
  {
    m_scene_manager->onResize(cmd, size);
  }

  //---------------------------------------------------------------------------------------------------------------
  // Rendering the scene
  // The scene is rendered to a GBuffer and the GBuffer is displayed in the ImGui window.
  // Only the ImGui is rendered to the swapchain image.
  // - Called every frame
  void onRender(VkCommandBuffer cmd) override
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Update the scene information buffer, this cannot be done in between dynamic rendering
    updateSceneBuffer(cmd);
    m_scene_manager->render(cmd, m_useRayTracing);
    postProcess(cmd);
  }

  // Apply post-processing
  void postProcess(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    m_scene_manager->post_process(cmd);

    // Barrier to make sure the image is ready for been display
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
  }

  //---------------------------------------------------------------------------------------------------------------
  // This renders the toolbar of the window
  // - Called when the ImGui menu is rendered
  void onUIMenu() override
  {
    bool reload = false;
    if (ImGui::BeginMenu("Tools"))
    {
      reload |= ImGui::MenuItem("Reload Shaders", "F5");
      ImGui::EndMenu();
    }
    reload |= ImGui::IsKeyPressed(ImGuiKey_F5);
    if (reload)
    {
      vkQueueWaitIdle(m_app->getQueue(0).queue);
      m_scene_manager->reload(m_useRayTracing);
    }
  }

  //---------------------------------------------------------------------------------------------------------------
  // The update of scene information buffer (UBO)
  //
  void updateSceneBuffer(VkCommandBuffer cmd)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight
    const glm::mat4& viewMatrix = m_scene_manager->camera()->getViewMatrix();
    const glm::mat4& projMatrix = m_scene_manager->camera()->getPerspectiveMatrix();

    m_scene_manager->gltf_resources().sceneInfo.viewProjMatrix =
        projMatrix * viewMatrix;  // Combine the view and projection matrices
    m_scene_manager->gltf_resources().sceneInfo.projInvMatrix =
        glm::inverse(projMatrix);  // Inverse projection matrix
    m_scene_manager->gltf_resources().sceneInfo.viewInvMatrix =
        glm::inverse(viewMatrix);  // Inverse view matrix
    m_scene_manager->gltf_resources().sceneInfo.cameraPosition =
        m_scene_manager->camera()->getEye();  // Get the camera position
    m_scene_manager->gltf_resources().sceneInfo.instances =
        (shaderio::GltfInstance*) m_scene_manager->gltf_resources()
            .bInstances.address;  // Get the address of the instance buffer
    m_scene_manager->gltf_resources().sceneInfo.meshes =
        (shaderio::GltfMesh*) m_scene_manager->gltf_resources()
            .bMeshes.address;  // Get the address of the mesh buffer
    m_scene_manager->gltf_resources().sceneInfo.materials =
        (shaderio::GltfMetallicRoughness*) m_scene_manager->gltf_resources()
            .bMaterials.address;  // Get the address of the material buffer

    // Making sure the scene information buffer is updated before rendering
    // Wait that the fragment shader is done reading the previous scene information and wait for the
    // transfer to complete
    nvvk::cmdBufferMemoryBarrier(cmd, {m_scene_manager->gltf_resources().bSceneInfo.buffer,
                                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                       VK_PIPELINE_STAGE_2_TRANSFER_BIT});
    vkCmdUpdateBuffer(cmd, m_scene_manager->gltf_resources().bSceneInfo.buffer, 0,
                      sizeof(shaderio::GltfSceneInfo),
                      &m_scene_manager->gltf_resources().sceneInfo);
    nvvk::cmdBufferMemoryBarrier(cmd, {m_scene_manager->gltf_resources().bSceneInfo.buffer,
                                       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
  }

  void onLastHeadlessFrame() override
  {
    m_app->saveImageToFile(m_scene_manager->get_image(eImgTonemapped),
                           m_vulkan_backend->gbuffers().getSize(),
                           nvutils::getExecutablePath().replace_extension(".jpg").string());
  }

  // Accessor for camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> getCameraManipulator() const { return m_cameraManip; }

private:
  // Application and core components
  nvapp::Application* m_app{};  // The application framework
  nvvk::ResourceAllocator
      m_allocator{};  // Resource allocator for Vulkan resources, used for buffers and images
  nvvk::StagingUploader m_stagingUploader{};  // Utility to upload data to the GPU

  std::unique_ptr<SceneManager> m_scene_manager = nullptr;
  std::shared_ptr<VulkanBackend> m_vulkan_backend = nullptr;
  std::shared_ptr<nvutils::CameraManipulator> m_cameraManip{
      std::make_shared<nvutils::CameraManipulator>()};

  // Ray tracing toggle
  bool m_useRayTracing = true;  // Set to true to use ray tracing, false for rasterization
};

//---------------------------------------------------------------------------------------------------------------
// The main function, entry point of the application
int main(int argc, char** argv)
{
  nvapp::ApplicationCreateInfo appInfo{};

  // Parsing the command line
  nvutils::ParameterParser cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  cli.add(reg);
  cli.parse(argc, argv);

  // Setting up the Vulkan context, instance and device extensions
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};

  // Add ray tracing features
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
               &accelFeature},  // Build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
               &rtPipelineFeature},                              // Use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},  // Required by ray tracing pipeline
          },
  };
  if (!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

  // Adding control on the validation layers
  nvvk::ValidationSettings validationSettings;
  validationSettings.setPreset(nvvk::ValidationSettings::LayerPresets::eStandard);
  vkSetup.instanceCreateInfoExt = validationSettings.buildPNextChain();

#if defined(USE_NSIGHT_AFTERMATH)
  // Adding the Aftermath extension to the device and initialize the Aftermath
  auto& aftermath = AftermathCrashTracker::getInstance();
  aftermath.initialize();
  aftermath.addExtensions(vkSetup.deviceExtensions);
  // The callback function is called when a validation error is triggered. This will wait to give
  // time to dump the GPU crash.
  nvvk::CheckError::getInstance().setCallbackFunction([&](VkResult result)
                                                      { aftermath.errorCallback(result); });
#endif

  // Initialize the Vulkan context
  nvvk::Context vkContext;
  if (vkContext.init(vkSetup) != VK_SUCCESS)
  {
    LOGE("Error in Vulkan context creation\n");
    return 1;
  }

  // Setting up the application
  appInfo.name = "Hello World";
  appInfo.instance = vkContext.getInstance();
  appInfo.device = vkContext.getDevice();
  appInfo.physicalDevice = vkContext.getPhysicalDevice();
  appInfo.queues = vkContext.getQueueInfos();

  // Create the application
  nvapp::Application application;
  application.init(appInfo);

  // Elements added to the application
  auto tutorial = std::make_shared<RtBasic>();  // Our tutorial element

  auto elemCamera =
      std::make_shared<nvapp::ElementCamera>();  // Element to control the camera movement
  auto windowTitle =
      std::make_shared<nvapp::ElementDefaultWindowTitle>();  // Element displaying the window title
                                                             // with application name and size
  auto windowMenu =
      std::make_shared<nvapp::ElementDefaultMenu>();  // Element displaying a menu, File->Exit ...
  auto camManip = tutorial->getCameraManipulator();
  elemCamera->setCameraManipulator(camManip);

  // Adding all elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  application.addElement(tutorial);

  application.run();     // Start the application, loop until the window is closed
  application.deinit();  // Closing application
  vkContext.deinit();    // De-initialize the Vulkan context

  return 0;
}
