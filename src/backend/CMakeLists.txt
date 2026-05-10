# -------------------------------------------------------------------
# 1. Header-Only Interface Library
# -------------------------------------------------------------------
add_library(backend_interfaces INTERFACE)

target_include_directories(backend_interfaces INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interfaces>
)

# Propagate PROFILE_APP to all consumers (app_lib, vk_backend, vk_render, …)
# so that #ifdef PROFILE_APP-guarded class members (IAppElement, IRenderBackend,
# VulkanBackend) are compiled consistently and do not cause ODR violations.
target_compile_definitions(backend_interfaces INTERFACE
    $<$<CONFIG:Debug>:PROFILE_APP>
    $<$<CONFIG:RelWithDebInfo>:PROFILE_APP>
)



if(APPLE)
    add_subdirectory(metal)
else()
    add_subdirectory(vulkan)
endif()
