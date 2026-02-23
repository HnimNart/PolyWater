add_library(render_interfaces
    ShaderManager.cpp
    ShaderManager.hpp
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

add_subdirectory(vulkan)
