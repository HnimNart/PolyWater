# -------------------------------------------------------------------
# 2. Vulkan Implementation (Shared Plugin)
# -------------------------------------------------------------------
set(LIB_NAME vulkan_plugin)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "*/*.cpp" 
    "*/*.hpp" 
    "*/*.h"
)

add_library(${LIB_NAME} SHARED ${PLUGIN_SOURCES})

# Basic Target Properties
set_target_properties(${LIB_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
)


target_compile_options(${LIB_NAME} PRIVATE -w)
# -------------------------------------------------------------------
# 3. Linking & Include logic
# -------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        ${ROOT_DIR}/ext/tinygltf
        ${ROOT_DIR}/ext/stb
        ${SHADER_OUTPUT_DIR}/../
        ${SHADER_OUTPUT_DIR}
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/nvvk/>
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
        vma
        offsetAllocator
        Slang
        vulkan      
)

# Proxy for LSP indexing
add_library(${LIB_NAME}_ide INTERFACE)
target_sources(${LIB_NAME}_ide INTERFACE ${PLUGIN_SOURCES})
target_link_libraries(${LIB_NAME}_ide INTERFACE ${LIB_NAME})
set_target_properties(${LIB_NAME}_ide PROPERTIES UNITY_BUILD OFF)
