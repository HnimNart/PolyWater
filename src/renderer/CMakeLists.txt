add_library(render_interfaces
    interfaces/IDeviceAssets.hpp
    interfaces/IRenderer.hpp
    interfaces/IRenderGraph.hpp
    interfaces/IToneMapper.hpp

    ShaderManager.cpp
    ShaderManager.hpp
)

target_link_libraries(render_interfaces
    PUBLIC
        glm
    PRIVATE
        core_lib    
        fmt
        Slang
)

target_include_directories(render_interfaces
    PRIVATE    
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
        ${SHADER_OUTPUT_DIR}/../
        ${SHADER_OUTPUT_DIR}
    )

# Set up include paths
target_include_directories(render_interfaces PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)


# Only add the Vulkan subdirectory if the platform is Linux
if(UNIX AND NOT APPLE)
    message(STATUS "Linux detected: adding Vulkan components")
    add_subdirectory(vulkan)
endif()

# Add the Metal renderer on Apple platforms
if(APPLE)
    message(STATUS "Apple detected: adding Metal renderer")
    add_subdirectory(metal)
endif()
