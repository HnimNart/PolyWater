# -------------------------------------------------------------------
# 2. Vulkan Implementation (Shared Plugin)
# -------------------------------------------------------------------
set(LIB_NAME vk_backend)

# NEW: Use a more specific glob or list directories manually to avoid nvvk
# By excluding nvvk/* from this glob, we avoid duplicate symbols
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "compiler/*.cpp" "compiler/*.hpp" "compiler/*.h"
    "core/*.cpp" "core/*.hpp" "core/*.h"
    "gui/*.cpp" "gui/*.hpp" "gui/*.h"
)

# imgui_impl_vulkan is compiled here (not inside the generic imgui target) so
# that its volk function-pointer references are resolved from THIS library's
# statically-linked volk copy.  If it were compiled inside the imgui STATIC
# target it would propagate volk into libapp_lib.so, creating a duplicate set
# of volk globals and leaving vkCreateInstance NULL in libvk_render.so.
list(APPEND PLUGIN_SOURCES "${ROOT_DIR}/ext/imgui/backends/imgui_impl_vulkan.cpp")

add_library(${LIB_NAME} STATIC ${PLUGIN_SOURCES})

# Add the nvvk subdirectory
add_subdirectory(nvvk)


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
        nvvk   
    PRIVATE
        core_lib            
        glm
        imgui
        vma
        offsetAllocator
        Slang
)

# Required for imgui_impl_vulkan.cpp to use volk's function pointers instead
# of directly-linked Vulkan prototypes.
target_compile_definitions(${LIB_NAME} PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)

