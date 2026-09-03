set(LIB_NAME app_lib)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" 
    "*.hpp" 
    "*.h"
    "*.mm"
)

add_library(${LIB_NAME} SHARED ${PLUGIN_SOURCES})

if(APPLE)
    set_source_files_properties(widgets/file_dialog_macos.mm PROPERTIES COMPILE_FLAGS "-x objective-c++")
    
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(UTI_FRAMEWORK UniformTypeIdentifiers REQUIRED)
    
    target_link_libraries(${LIB_NAME} PRIVATE 
        ${APPKIT_FRAMEWORK} 
        ${FOUNDATION_FRAMEWORK}
        ${UTI_FRAMEWORK}
    )
endif()

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

if(TARGET CUDA::nvml)
    target_link_libraries(${LIB_NAME} PUBLIC CUDA::nvml)
    target_compile_definitions(${LIB_NAME} PUBLIC NVML_SUPPORTED)
    message(STATUS "[${LIB_NAME}] NVML Support: ENABLED")
endif()
