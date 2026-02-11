set(LIB_NAME common)

include(CompilerSlangShader)

###################################
# Sources
###################################
file(GLOB_RECURSE LIB_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

message(STATUS "====> LIB_SOURCES:")
foreach(src IN LISTS LIB_SOURCES)
    message(STATUS "  ${src}")
endforeach()

file(GLOB_RECURSE SHADER_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang"
)



###################################
# Library target
###################################
add_library(${LIB_NAME})

target_sources(${LIB_NAME}
    PRIVATE
        ${LIB_SOURCES}
        ${SHADER_SOURCES}
)

target_compile_definitions(${LIB_NAME} PRIVATE
    $<$<CONFIG:Debug>:ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:ENABLE_PROFILING>
)

find_package(nlohmann_json 3.10.5 REQUIRED)
find_package(Slang)

###################################
# Include paths
###################################
target_include_directories(${LIB_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
     # Add the path to tinygltf
    "${PROJECT_SOURCE_DIR}/ext/nvpro_core2/third_party/tinygltf"

    # ✅ ADD THIS: Add the path to stb_image
    "${PROJECT_SOURCE_DIR}/ext/nvpro_core2/third_party/stb"

    ${Slang_INCLUDE_DIR}
)

###################################
# Dependencies
###################################

target_link_libraries(${LIB_NAME}
    PUBLIC
        glm
        volk
        glfw   # Windowing library (Application needs it)
        glm    # Math library
        vma    # Vulkan Memory Allocator
        imgui  # Because GBuffer needs it (returns the ImTexture and uses ImGui_ImplVulkan_AddTexture
        implot # For Implot
        fmt    # For formatting strings
        nvvk
        imgui
        Slang
        nlohmann_json::nlohmann_json
)

###################################
# IDE organization
###################################
source_group("Shaders" FILES ${SHADER_SOURCES})

###################################
#### Shader compilation
###################################
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/_autogen")

file(GLOB_RECURSE SHADER_GLSL_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.glsl")
file(GLOB_RECURSE SHADER_SLANG_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.slang")
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")

message(STATUS "====> SHADER_SOURCES:")
foreach(src IN LISTS SHADER_SOURCES)
    message(STATUS "  ${src}")
endforeach()

set(SHADER_INCLUDE_FLAGS
    "-I${NVSHADERS_DIR}"
    "-I${ROOT_DIR}"
    "-I${SHADER_DIR}"
    "-I${CMAKE_CURRENT_SOURCE_DIR}"
)

compile_slang(
    "${SHADER_SLANG_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_SLANG_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

compile_glsl(
    "${SHADER_GLSL_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_GLSL_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

###################################
# Attach generated shaders to lib
###################################
target_sources(${LIB_NAME}
    PRIVATE
        ${SHADER_SLANG_FILES}
        ${SHADER_GLSL_FILES}
        ${GENERATED_SHADER_SLANG_HEADERS}
        ${GENERATED_SHADER_GLSL_HEADERS}
)

source_group("Shaders/Compiled"
    FILES
        ${GENERATED_SHADER_SLANG_HEADERS}
        ${GENERATED_SHADER_GLSL_HEADERS}
)
