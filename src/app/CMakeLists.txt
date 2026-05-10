set(LIB_NAME app_lib)

# Update GLOB to include .mm files for macOS
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" 
    "*.hpp" 
    "*.h"
    "*.mm" # Added Objective-C++ files
)

add_library(${LIB_NAME} SHARED ${PLUGIN_SOURCES})

# -------------------------------------------------------------------
# Platform-specific setup for Apple (macOS)
# -------------------------------------------------------------------
if(APPLE)
    # Ensure .mm files are treated as Objective-C++
    set_source_files_properties(widgets/file_dialog_macos.mm PROPERTIES COMPILE_FLAGS "-x objective-c++")
    
    # Link the native UI frameworks required for file dialogs
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(UTI_FRAMEWORK UniformTypeIdentifiers REQUIRED)
    
    target_link_libraries(${LIB_NAME} PRIVATE 
        ${APPKIT_FRAMEWORK} 
        ${FOUNDATION_FRAMEWORK}
        ${UTI_FRAMEWORK}
    )
endif()

# -------------------------------------------------------------------
# Linking & Include logic
# -------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        ${Vulkan_VOLK_DIR}
        ${Vulkan_VOLK_DIR}/..
)

target_link_libraries(${LIB_NAME}
    PUBLIC
        imgui
        implot
    PRIVATE
        backend_interfaces  
        core_lib
        scene_lib
)

# Optional CUDA NVML support
if(TARGET CUDA::nvml)
    target_link_libraries(${LIB_NAME} PUBLIC CUDA::nvml)
    target_compile_definitions(${LIB_NAME} PUBLIC NVML_SUPPORTED)
    message(STATUS "[${LIB_NAME}] NVML Support: ENABLED")
endif()
