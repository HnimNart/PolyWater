# -------------------------------------------------------------------
# 2. Vulkan Implementation (Shared Plugin)
# -------------------------------------------------------------------
set(LIB_NAME vk_render)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" "*.hpp" "*.h"
)

add_library(${LIB_NAME} SHARED ${PLUGIN_SOURCES})


# Link it to your project (Replace 'VulkanRenderer' with your actual target name!)
include(DownloadOIDN)
target_link_libraries(${LIB_NAME} PRIVATE OpenImageDenoise)

# On Windows, the .dll files must be next to your .exe to run.
if(WIN32)
    add_custom_command(TARGET VulkanRenderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OIDN_INSTALL_DIR}/bin/OpenImageDenoise.dll"
        $<TARGET_FILE_DIR:VulkanRenderer>
        
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OIDN_INSTALL_DIR}/bin/tbb.dll" 
        $<TARGET_FILE_DIR:VulkanRenderer>
        
        COMMENT "Copying OIDN and TBB DLLs to output directory..."
    )
endif()

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
        volk  # SHARED volk: must be explicit so libvolk.so appears in DT_NEEDED
    PRIVATE
        core_lib
        scene_lib
        vk_backend
        imgui
        glm
)

