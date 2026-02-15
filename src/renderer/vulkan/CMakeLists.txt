# -------------------------------------------------------------------
# 2. Vulkan Implementation (Shared Plugin)
# -------------------------------------------------------------------
set(LIB_NAME vk_render)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" "*.hpp" "*.h"
)

add_library(${LIB_NAME} SHARED ${PLUGIN_SOURCES})

# Basic Target Properties
set_target_properties(${LIB_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
)

# -------------------------------------------------------------------
# 3. Linking & Include logic
# -------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        ${SHADER_OUTPUT_DIR}/../
        ${SHADER_OUTPUT_DIR}
        ${Slang_INCLUDE_DIR}
)

target_link_libraries(${LIB_NAME}
    PUBLIC
        backend_interfaces
        render_interfaces
        nvvk
    PRIVATE
        core_lib
        scene_lib
        vk_backend
)

