# -------------------------------------------------------------------
# 1. Header-Only Interface Library
# -------------------------------------------------------------------
add_library(backend_interfaces INTERFACE)

target_include_directories(backend_interfaces INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interfaces>
)



if(APPLE)
    add_subdirectory(metal)
else()
    add_subdirectory(vulkan)
endif()
