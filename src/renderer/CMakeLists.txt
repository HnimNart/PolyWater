add_library(render_interfaces
    interfaces/device_assets_interface.hpp
    interfaces/renderer_interface.hpp
    interfaces/render_graph_interface.hpp
    interfaces/render_graph_interface.cpp
    interfaces/tone_mapper_interface.hpp

    shader_manager.cpp
    shader_manager.hpp
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
