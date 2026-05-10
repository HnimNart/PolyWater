# -------------------------------------------------------------------
# App: apples (macOS – Metal backend, no Vulkan runtime)
# -------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22...3.31)

get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME} LANGUAGES C CXX OBJC OBJCXX)
message(STATUS "Processing App: ${PROJECT_NAME}")

file(GLOB EXE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.mm" "*.md")

add_executable(${PROJECT_NAME} ${EXE_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    metal_backend
    common
    glfw
)

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

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-unguarded-availability-new
    -fno-objc-arc
)

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Samples")
source_group("Source Files" FILES ${EXE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}
)
