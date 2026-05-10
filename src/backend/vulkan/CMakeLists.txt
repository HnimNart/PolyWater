set(LIB_NAME vk_backend)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "compiler/*.cpp" "compiler/*.hpp" "compiler/*.h"
    "core/*.cpp" "core/*.hpp" "core/*.h"
    "gui/*.cpp" "gui/*.hpp" "gui/*.h"
)

add_library(${LIB_NAME} STATIC ${PLUGIN_SOURCES})

add_subdirectory(nvvk)

target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        ${SHADER_OUTPUT_DIR}/../
        ${SHADER_OUTPUT_DIR}
        ${Slang_INCLUDE_DIR}
        ${Vulkan_VOLK_DIR}
        ${Vulkan_VOLK_DIR}/..
)

target_link_libraries(${LIB_NAME}
    PUBLIC
        backend_interfaces
        nvvk   
    PRIVATE
        core_lib            
        glm
        imgui
        vma
        offsetAllocator
        Slang
)
