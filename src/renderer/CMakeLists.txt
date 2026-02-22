# -------------------------------------------------------------------
# 1. Header-Only Interface Library
# -------------------------------------------------------------------
add_library(render_interfaces INTERFACE)

target_include_directories(render_interfaces INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interfaces>
)

add_subdirectory(vulkan)
