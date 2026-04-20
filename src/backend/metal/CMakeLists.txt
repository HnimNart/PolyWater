# -------------------------------------------------------------------
# Metal Implementation (Shared Plugin)
# -------------------------------------------------------------------
set(LIB_NAME metal_backend)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "core/*.cpp" 
    "core/*.hpp" 
    "core/*.h"
)

add_library(${LIB_NAME} STATIC ${PLUGIN_SOURCES})

# -------------------------------------------------------------------
# Platform-specific setup for Apple
# -------------------------------------------------------------------
if(APPLE)
    # 1. Automate metal-cpp download
    include(FetchContent)
    FetchContent_Declare(
        metal_cpp
        GIT_REPOSITORY https://github.com/bkaradzic/metal-cpp.git
        GIT_TAG        metal-cpp_26
    )
    FetchContent_MakeAvailable(metal_cpp)

    # 2. Add metal-cpp headers to the include path
    target_include_directories(${LIB_NAME} SYSTEM PUBLIC ${metal_cpp_SOURCE_DIR})

    # 3. Find Apple system frameworks
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(METALKIT_FRAMEWORK MetalKit REQUIRED)

    # 4. Link frameworks
    target_link_libraries(${LIB_NAME} PRIVATE 
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${METALKIT_FRAMEWORK}
    )

    # 5. Compiler flags for Metal-CPP
    target_compile_options(${LIB_NAME} PRIVATE 
        -Wno-unguarded-availability-new
        -fno-objc-arc 
    )
endif()

# -------------------------------------------------------------------
# General Linking & Include logic
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
    PRIVATE
        core_lib            
        glm
        imgui
        Slang
)

