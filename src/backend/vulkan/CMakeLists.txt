# -------------------------------------------------------------------
# 2. Vulkan Implementation (Shared Plugin)
# -------------------------------------------------------------------
set(LIB_NAME vulkan_plugin)

# NEW: Use a more specific glob or list directories manually to avoid nvvk
# By excluding nvvk/* from this glob, we avoid duplicate symbols
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "compiler/*.cpp" "compiler/*.hpp" "compiler/*.h"
    "core/*.cpp" "core/*.hpp" "core/*.h"
    "gui/*.cpp" "gui/*.hpp" "gui/*.h"
    "render/*.cpp" "render/*.hpp" "render/*.h"
)

add_library(${LIB_NAME} SHARED ${PLUGIN_SOURCES})

# Add the nvvk subdirectory
add_subdirectory(nvvk)

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
        core_lib            
        scene_lib           
        volk
        glm
        imgui
        nvvk   
        vma
        offsetAllocator
        Slang
)

# Proxy for LSP indexing
add_library(${LIB_NAME}_ide INTERFACE)
target_sources(${LIB_NAME}_ide INTERFACE ${PLUGIN_SOURCES})
target_link_libraries(${LIB_NAME}_ide INTERFACE ${LIB_NAME})
set_target_properties(${LIB_NAME}_ide PROPERTIES UNITY_BUILD OFF)
