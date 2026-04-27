# -------------------------------------------------------------------
# App: apples (macOS – Metal backend, no Vulkan runtime)
# -------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22...3.31)

# 1. Setup Project
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME} LANGUAGES C CXX OBJC OBJCXX)
message(STATUS "Processing App: ${PROJECT_NAME}")

# 2. Sources (include Objective-C++ .mm files alongside plain C++)
file(GLOB EXE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.mm" "*.md")

# 3. Define Executable
add_executable(${PROJECT_NAME} ${EXE_SOURCES})

# 4. Link Dependencies (Metal backend + windowing; no Vulkan renderer)
target_link_libraries(${PROJECT_NAME} PRIVATE
    metal_backend   # MetalContextManager + metal-cpp headers
    common # ApplicationCreateInfo and common app types
    glfw            # GLFW windowing (GLFW_NO_API / Metal surface)
)

# 5. Apple system frameworks required by Metal
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(METAL_FRAMEWORK      Metal      REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(APPKIT_FRAMEWORK     AppKit     REQUIRED)
find_library(METALKIT_FRAMEWORK   MetalKit   REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${FOUNDATION_FRAMEWORK}
    ${METAL_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
    ${METALKIT_FRAMEWORK}
)

# 6. Compiler flags matching the metal_backend settings
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-unguarded-availability-new
    -fno-objc-arc
)

# 7. IDE Organization
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Samples")
source_group("Source Files" FILES ${EXE_SOURCES})

# 8. Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}
)
