# -------------------------------------------------------------------
# Metal Renderer (Apple-only)
# -------------------------------------------------------------------
if(NOT APPLE)
    return()
endif()

set(LIB_NAME metal_renderer)

file(GLOB_RECURSE METAL_RENDERER_SOURCES CONFIGURE_DEPENDS
    "*.hpp"
    "*.mm"
    "passes/*.hpp"
    "passes/*.mm"
)

add_library(${LIB_NAME} STATIC ${METAL_RENDERER_SOURCES})

# Frameworks
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(METAL_FRAMEWORK      Metal      REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)

target_link_libraries(${LIB_NAME}
    PUBLIC
        render_interfaces      # IRenderer, IRenderGraph, IDeviceAssets …
        metal_backend          # MetalContextManager, MetalRenderContext, MetalBackend
    PRIVATE
        scene_lib              # SceneResourcesManager, Scene, SceneLoader …
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
)

target_include_directories(${LIB_NAME}
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        ${SHADER_OUTPUT_DIR}/../
        ${SHADER_OUTPUT_DIR}
)

target_compile_options(${LIB_NAME} PRIVATE
    -Wno-unguarded-availability-new
    -fno-objc-arc
)

set_property(TARGET ${LIB_NAME} PROPERTY FOLDER "Renderer")
source_group("passes" REGULAR_EXPRESSION "passes/.*")
